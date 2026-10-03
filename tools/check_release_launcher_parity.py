#!/usr/bin/env python3
"""Static gate: every release-packaging job emits a launcher before verifying,
and every emitted launcher satisfies the launcher contract.

The Windows packaging step cannot be executed from a Linux runner, so
"the Windows job is fixed" is otherwise unverifiable until someone reads
a CI log. This gate makes the contract statically checkable on any
machine. It does four things:

1. Workflow coverage -- reads .github/workflows/ci.yml and asserts that
   every step which runs tools/verify_release_package.py also runs
   tools/make_release_launchers.py, with a platform that matches the job's
   runner. The workflow is read by the small strict reader below, not by
   PyYAML: this gate also runs inside the Windows release test job, whose
   interpreter has no third-party packages, and `import yaml` there killed
   the whole job before it could check anything.
2. Name parity -- the launcher names the helper emits must be exactly the
   names the verifier accepts, so a rename on either side fails here instead
   of silently turning the release gate red on one platform only.
3. CONTENT parity -- the actual launcher TEXT must satisfy
   tools/launcher_contract.py. Before P2-16 this file only checked that the
   .bat used CRLF; deleting `cd /d "%~dp0"` or corrupting the exe name left
   this gate, tools/verify_release_package.py and tests/test_release_package.py
   all green. That is a broken launcher shipping, so the lines that make the
   .bat work are now asserted here.
4. Write-path parity -- tools/make_release_launchers.py is actually RUN into
   a temporary release root, and the bytes that land on disk are re-checked
   against the same contract. The constants can be right while the writer
   mangles them (newline translation is exactly how CRLF dies).
5. Host-independent line endings -- run_game.sh is LF and run_game.bat is
   CRLF on EVERY platform. A text-mode write maps every "\n" onto the host's
   os.linesep, which is why a CR-carrying run_game.sh reached the Windows
   jobs and turned two gates red there while Linux, sanitizer and every arm64
   job stayed green. Step 4 cannot catch that from a Linux runner, because
   os.linesep is "\n" here and the bytes look right. Step 5 models the Windows
   write filter and asserts the launchers survive it.

Exit 0 = parity holds. Exit 1 = drift (diagnostics on stderr).
"""

import contextlib
import builtins
import importlib.util
import inspect
import io
import os
import re
import sys
import tempfile


ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import launcher_contract as contract  # noqa: E402

WORKFLOW = os.path.join(ROOT, ".github", "workflows", "ci.yml")
VERIFIER = os.path.join(ROOT, "tools", "verify_release_package.py")
LAUNCHER_TOOL = os.path.join(ROOT, "tools", "make_release_launchers.py")
# The editor ships its own copy of the launcher text
# (editor/Services/ProjectBuildService.cs). It is a second producer of the
# same artifact, so it is checked against the same contract here rather than
# being allowed to drift silently.
EDITOR_BUILD_SERVICE = os.path.join(ROOT, "editor", "Services",
                                   "ProjectBuildService.cs")

VERIFIER_REF = "tools/verify_release_package.py"
LAUNCHER_REF = "tools/make_release_launchers.py"

# A windows runner must receive the .bat; every other runner here is
# POSIX and must receive at least the .sh. "both" is accepted on POSIX
# because the portable release layout ships the pair.
POSIX_PLATFORMS = {"posix", "both"}
WINDOWS_PLATFORMS = {"windows"}


def fail(message):
    print("[LauncherParity] ERROR: " + message, file=sys.stderr)


class WorkflowFormatError(ValueError):
    """The workflow uses YAML this reader does not implement.

    Raised, never swallowed. A reader that skipped what it did not
    understand would return FEWER packaging steps than the file really
    has, and the missing ones would silently stop being checked -- a
    gate that quietly stops looking is worse than one that fails, because
    it reports green while covering nothing.
    """


# Every YAML token this reader accepts. Anything outside it is an error
# above, not a shrug.
_KEY = re.compile(r"[A-Za-z0-9_][A-Za-z0-9_.-]*")
_BLOCK_SCALAR = re.compile(r"\|([+-]?)")
_PLAIN_FORBIDDEN = "[]{}&*!|>%@`"


def _indent_of(line):
    return len(line) - len(line.lstrip(" "))


def _is_ignorable(line):
    """Blank lines and whole-line comments carry no structure."""
    stripped = line.strip()
    return not stripped or stripped.startswith("#")


def _parse_scalar(text, line_number):
    """A single-line scalar value: plain, 'single' or "double" quoted."""
    value = text.strip()
    if not value:
        return None
    if value[0] in "'\"":
        quote = value[0]
        if len(value) < 2 or value[-1] != quote:
            raise WorkflowFormatError(
                f"line {line_number}: unterminated quoted scalar {text!r}")
        return value[1:-1]
    if value[0] in _PLAIN_FORBIDDEN:
        raise WorkflowFormatError(
            f"line {line_number}: unsupported YAML construct starting with "
            f"{value[0]!r} in {text!r}")
    # A '#' only opens a comment when it starts the line or follows a
    # space; 'a#b' is a plain scalar, '#tag' and 'x #y' are not.
    comment = re.search(r"(?:^|\s)#", value)
    if comment:
        value = value[:comment.start()].strip()
    return value or None


def _read_block_scalar(reader, index, parent_indent, line_number):
    """Consume a `|` literal block scalar body and return its text.

    Only literal (`|`) is supported. Folded (`>`) changes line breaks and
    would silently alter the `run:` text this gate substring-matches, so
    it is rejected rather than approximated.
    """
    body = []
    while index < len(reader):
        line = reader[index]
        if not line.strip():
            body.append("")
            index += 1
            continue
        if _indent_of(line) <= parent_indent:
            break
        body.append(line)
        index += 1
    # Trailing blank lines belong to the surrounding document, not the
    # block; YAML's own clip/strip rules make them insignificant anyway.
    while body and not body[-1].strip():
        body.pop()
    if not body:
        raise WorkflowFormatError(
            f"line {line_number}: block scalar has no body")
    block_indent = min(_indent_of(line) for line in body if line.strip())
    return "\n".join(line[block_indent:] for line in body), index


class _Reader:
    """The workflow lines plus the few that were rewritten for parsing.

    A sequence item written as `- key: value` carries its first key on the
    dash line, so reading it as a mapping means treating the dash as two
    spaces. That rewrite lives in `overlay` instead of being applied to
    `lines`: the caller's list is never modified, which keeps the `- `
    count in the anti-vacuity check honest no matter the order the two
    run in.
    """

    def __init__(self, lines):
        self.lines = lines
        self.overlay = {}

    def __len__(self):
        return len(self.lines)

    def __getitem__(self, index):
        return self.overlay.get(index, self.lines[index])


def _parse_block(reader, index, indent):
    """Parse one mapping or sequence at the given indent.

    Returns (value, next_index). Raises WorkflowFormatError on anything
    it does not fully understand, so an unsupported construct surfaces as
    a red gate instead of a smaller list of steps.
    """
    # Skip blanks/comments to find the first real line of this block.
    while index < len(reader) and _is_ignorable(reader[index]):
        index += 1
    if index >= len(reader) or _indent_of(reader[index]) < indent:
        return None, index

    if reader[index].lstrip(" ").startswith("- "):
        return _parse_sequence(reader, index, indent)
    return _parse_mapping(reader, index, indent)


def _parse_sequence(reader, index, indent):
    items = []
    while index < len(reader):
        if _is_ignorable(reader[index]):
            index += 1
            continue
        current = _indent_of(reader[index])
        if current < indent:
            break
        if current > indent:
            raise WorkflowFormatError(
                f"line {index + 1}: unexpected indentation inside a sequence")
        body = reader[index].lstrip(" ")
        if not body.startswith("- "):
            break
        content = body[2:]
        if re.match(r"([^:\s][^:]*?):(?:\s|$)", content):
            # `- key: value` opens a mapping whose remaining keys sit two
            # columns in, exactly where blanking the dash puts this one.
            # `steps:` is a list of these, and each step's `run:` follows.
            item_indent = current + 2
            reader.overlay[index] = " " * item_indent + content
            item, index = _parse_mapping(reader, index, item_indent)
        else:
            item = _parse_scalar(content, index + 1)
            index += 1
        items.append(item)
    return items, index


def _parse_mapping(reader, index, indent):
    mapping = {}
    while index < len(reader):
        if _is_ignorable(reader[index]):
            index += 1
            continue
        current = _indent_of(reader[index])
        if current < indent:
            break
        if current > indent:
            raise WorkflowFormatError(
                f"line {index + 1}: unexpected indentation inside a mapping")
        stripped = reader[index].lstrip(" ")
        if stripped.startswith("- "):
            break
        match = re.match(r"([^:\s][^:]*?):(?:\s+(.*))?$", stripped)
        if not match:
            raise WorkflowFormatError(
                f"line {index + 1}: {stripped!r} is not a `key: value` entry")
        key = match.group(1).strip()
        if not _KEY.fullmatch(key):
            raise WorkflowFormatError(
                f"line {index + 1}: unsupported key {key!r}")
        rest = (match.group(2) or "").strip()
        line_number = index + 1
        index += 1

        if _BLOCK_SCALAR.fullmatch(rest):
            text, index = _read_block_scalar(reader, index, current, line_number)
            mapping[key] = text
            continue

        if rest.startswith(">"):
            raise WorkflowFormatError(
                f"line {line_number}: folded (`>`) block scalars are not "
                f"supported; this gate matches `run:` text verbatim")
        if rest and rest[0] in "[{":
            raise WorkflowFormatError(
                f"line {line_number}: flow collections are not supported")

        # A key with nothing after the colon owns the block nested below
        # it. That block's indent is whatever the next real line uses,
        # which need not be exactly one column deeper.
        nested = index
        while nested < len(reader) and _is_ignorable(reader[nested]):
            nested += 1
        if rest == "" and nested < len(reader) and _indent_of(reader[nested]) > current:
            value, index = _parse_block(reader, nested, _indent_of(reader[nested]))
        else:
            value = _parse_scalar(rest, line_number)
        if key in mapping:
            raise WorkflowFormatError(
                f"line {line_number}: duplicate key {key!r}")
        mapping[key] = value
    return mapping, index


def load_workflow():
    """Parse the workflow into {job_name: {...}}.

    Deliberately hand-rolled. This gate runs inside the Windows release
    test job, whose interpreter has no PyYAML, and a missing third-party
    module there killed the whole job with a ModuleNotFoundError -- the
    exact failure this function exists to prevent. The workflow only uses
    mappings, `- ` sequences, plain/quoted scalars and `|` blocks; every
    one of those is parsed here, and anything else raises rather than
    being skipped.
    """
    with open(WORKFLOW, "r", encoding="utf-8") as handle:
        text = handle.read()
    lines = text.splitlines()
    document, consumed = _parse_block(_Reader(lines), 0, 0)
    while consumed < len(lines) and _is_ignorable(lines[consumed]):
        consumed += 1
    if consumed < len(lines):
        raise WorkflowFormatError(
            f"line {consumed + 1}: trailing content "
            f"{lines[consumed].strip()!r} was not parsed")

    jobs = (document or {}).get("jobs")
    if not jobs:
        raise ValueError("workflow defines no jobs")

    # Anti-vacuity: the number of sequence items the reader produced must
    # equal the number of `- ` lines in the file. `steps:` is the only
    # thing this gate reads out of the workflow, so a reader that quietly
    # dropped a step would narrow the gate while still reporting green.
    # Counting is order-independent now that parsing never rewrites the
    # caller's lines.
    expected_items = sum(
        1 for line in lines if line.lstrip(" ").startswith("- "))
    actual_items = _count_sequences(document)
    if actual_items != expected_items:
        raise WorkflowFormatError(
            f"the workflow reader parsed {actual_items} sequence item(s) but "
            f"the file contains {expected_items} `- ` line(s); the gate would "
            f"be checking fewer steps than the workflow declares")

    for name, job in jobs.items():
        if not isinstance(job, dict):
            raise WorkflowFormatError(
                f"job {name!r} is not a mapping but {type(job).__name__}")
    return jobs


def _count_sequences(node):
    """Total sequence items anywhere in the parsed document."""
    if isinstance(node, list):
        return len(node) + sum(_count_sequences(item) for item in node)
    if isinstance(node, dict):
        return sum(_count_sequences(value) for value in node.values())
    return 0


def load_launcher_module():
    spec = importlib.util.spec_from_file_location("rowl_make_release_launchers",
                                                  LAUNCHER_TOOL)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def step_run(step):
    run = step.get("run")
    return run if isinstance(run, str) else ""


def platform_argument(script):
    """Extract the --platform value from a shell snippet, if present."""
    match = re.search(r"--platform[= ]+([A-Za-z]+)", script)
    return match.group(1) if match else None


def runner_platform(runs_on):
    label = " ".join(runs_on) if isinstance(runs_on, list) else str(runs_on)
    if "windows" in label.lower():
        return "windows"
    if "ubuntu" in label.lower() or "linux" in label.lower() or "macos" in label.lower():
        return "posix"
    return "unknown"


def check_workflow():
    problems = []
    checked = []
    for job_name, job in load_workflow().items():
        runs_on = job.get("runs-on")
        expected = runner_platform(runs_on)
        steps = job.get("steps") or []
        for index, step in enumerate(steps):
            script = step_run(step)
            if VERIFIER_REF not in script:
                continue
            title = "%s / %s" % (job_name, step.get("name") or f"step[{index}]")
            checked.append(title)
            if LAUNCHER_REF not in script:
                problems.append(
                    f"{title}: runs the release verifier but never calls "
                    f"{LAUNCHER_REF} — the packaged release would reach the "
                    f"verifier with no launcher and the gate would fail")
                continue
            chosen = platform_argument(script)
            if chosen is None:
                problems.append(
                    f"{title}: calls {LAUNCHER_REF} without an explicit "
                    f"--platform; the platform is inferred from the host at "
                    f"runtime and cannot be audited statically")
                continue
            allowed = WINDOWS_PLATFORMS if expected == "windows" else POSIX_PLATFORMS
            if expected == "unknown":
                problems.append(
                    f"{title}: unrecognized runs-on {runs_on!r}; refusing to "
                    f"guess the launcher platform")
                continue
            if chosen not in allowed:
                problems.append(
                    f"{title}: runner {runs_on!r} requires one of "
                    f"{sorted(allowed)}, but the step requests {chosen!r}")
    if not checked:
        problems.append(
            f"no step in {os.path.relpath(WORKFLOW, ROOT)} runs "
            f"{VERIFIER_REF}; the packaging gate was renamed away and this "
            f"check is now vacuous")
    return checked, problems


def check_launcher_contract():
    """The emitted text must satisfy the launcher contract, and the names
    must be exactly the names the verifier accepts."""
    problems = []
    module = load_launcher_module()

    with open(VERIFIER, "r", encoding="utf-8") as handle:
        verifier_source = handle.read()

    player_names = contract.player_basenames()

    # The verifier must still consume the shared name lists, not a private
    # copy: that is what makes the loop above a real two-sided check.
    for reference in ("launcher_contract.LAUNCHER_NAMES",
                      "launcher_contract.PLAYER_NAMES"):
        if reference not in verifier_source:
            problems.append(
                f"tools/verify_release_package.py no longer reads "
                f"{reference}; the producer/consumer name contract is now "
                f"checked against nothing")

    windows_launcher = module.WINDOWS_LAUNCHER
    posix_launcher = module.POSIX_LAUNCHER

    # Content. This is the P2-16 addition: the .bat used to be checked for
    # CRLF and nothing else, so removing the working-directory pin or the
    # exe name was invisible here.
    problems.extend(contract.windows_launcher_problems(windows_launcher,
                                                       player_names))
    problems.extend(contract.posix_launcher_problems(posix_launcher,
                                                     player_names,
                                                     full=True))

    emitted = {module.POSIX_LAUNCHER_NAME, module.WINDOWS_LAUNCHER_NAME}
    accepted = contract.LAUNCHER_NAMES
    for name in sorted(emitted - accepted):
        problems.append(
            f"the helper emits {name!r} but the verifier does not accept it")
    for name in sorted(accepted - emitted):
        problems.append(
            f"the verifier accepts {name!r} but the helper never emits it")

    # The platform table must actually route each platform to its own
    # launcher. Without this, pointing --platform windows at the POSIX
    # launcher passes every content check below: the POSIX launcher satisfies
    # the contract, it is simply the wrong file, and the Windows release
    # would ship a shell script under a .bat name.
    expected = {
        "posix": (module.POSIX_LAUNCHER_NAME, module.POSIX_LAUNCHER),
        "windows": (module.WINDOWS_LAUNCHER_NAME, module.WINDOWS_LAUNCHER),
    }
    for platform, (name, text) in sorted(expected.items()):
        entries = module.LAUNCHERS.get(platform)
        if not entries:
            problems.append(f"the helper has no {platform!r} platform entry")
            continue
        names = {entry[0] for entry in entries}
        texts = {entry[1] for entry in entries}
        if names != {name}:
            problems.append(
                f"--platform {platform} would write {sorted(names)}, not "
                f"{name!r}; the Windows packaging step would ship the wrong "
                f"launcher")
        if texts != {text}:
            problems.append(
                f"--platform {platform} would write launcher content that is "
                f"not the {name!r} constant")

    # The writer must not be a text-mode write. This is the check that closes
    # the Windows-only bug: a text-mode write maps every "\n" onto the host's
    # os.linesep, so the SAME constant emits LF on Linux and CRLF on Windows.
    # os.linesep is "\n" on a Linux runner, so this gate's own byte comparison
    # cannot see it -- the corruption is invisible from every job that was
    # already passing. BINARY mode is the only form that cannot translate.
    source = inspect.getsource(module.emit)
    if 'write_launcher_bytes(' not in source:
        problems.append(
            "make_release_launchers.emit() no longer writes through "
            "launcher_contract.write_launcher_bytes(); a text-mode write maps "
            "every \\n onto the host's os.linesep, which ships run_game.sh "
            "with a CR on Windows while Linux stays green")

    # The constants must agree with the declared line endings BEFORE they are
    # written, so a wrong constant fails at the producer and not on a user's
    # machine.
    for name, text in ((module.POSIX_LAUNCHER_NAME, module.POSIX_LAUNCHER),
                       (module.WINDOWS_LAUNCHER_NAME, module.WINDOWS_LAUNCHER)):
        problems.extend(contract.newline_problems(
            name, text.encode("utf-8"), label=f"the {name} constant"))
    return problems


class _WindowsTextWriter:
    """A text-mode handle whose writes pass through the Windows filter."""

    def __init__(self, handle, seen):
        self._handle = handle
        self._seen = seen

    def write(self, data):
        self._seen.append(data)
        return self._handle.write(contract.windows_text_mode_write(data))

    def __enter__(self):
        self._handle.__enter__()
        return self

    def __exit__(self, *exc_info):
        return self._handle.__exit__(*exc_info)

    def __getattr__(self, name):
        return getattr(self._handle, name)


def _windows_open(seen):
    """An `open` that translates newlines on text-mode WRITES, like Windows.

    CPython rewrites every "\\n" in a text-mode write to os.linesep, which is
    "\\r\\n" on Windows. open() documents it; this reproduces it. Binary mode
    ("wb") is deliberately NOT affected, because CPython performs no newline
    translation in binary mode at all -- so a producer that writes binaries is
    genuinely immune to the host, and one that writes text is not.
    """
    real_open = builtins.open

    def patched(file, mode="r", *args, **kwargs):
        newline = kwargs.get("newline", None)
        writing = any(flag in mode for flag in ("w", "a", "+", "x"))
        translating = writing and "b" not in mode and newline is None
        handle = real_open(file, mode, *args, **kwargs)
        if not translating:
            return handle
        return _WindowsTextWriter(handle, seen)

    return patched


def check_line_endings_are_host_independent():
    """The producer's bytes must not depend on the machine that ran it.

    This is the Windows-only defect, reproduced and then ruled out without a
    Windows runner. open() is patched to translate newlines on text-mode
    writes -- exactly what a Windows host does -- and the REAL emit() is run
    through it. The bytes that reach disk must still satisfy the contract.

    Step 4 cannot do this: on a Linux runner os.linesep is "\\n", so the
    corruption is invisible to a byte comparison and the same code ships a
    broken run_game.sh to the Windows job. That is why this bug reached CI
    while every Linux, sanitizer and arm64 job stayed green.
    """
    problems = []
    module = load_launcher_module()

    # Self-test: if the patched open does not actually translate, the check
    # below would pass on a harness that simulates nothing.
    with tempfile.TemporaryDirectory(
            prefix="rowl-launcher-eol-",
            dir=os.environ.get("ROWL_TMPDIR")) as directory:
        probe = os.path.join(directory, "probe.txt")
        seen = []
        real_open = builtins.open
        builtins.open = _windows_open(seen)
        try:
            with builtins.open(probe, "w", encoding="utf-8") as handle:
                handle.write("a\n")
        finally:
            builtins.open = real_open
        with real_open(probe, "rb") as handle:
            probe_bytes = handle.read()
        if probe_bytes != b"a\r\n":
            problems.append(
                "the simulated-Windows open() no longer translates newlines "
                f"on text-mode writes (probe wrote {probe_bytes!r}, expected "
                f"b'a\\r\\n'); the host-independence check below is not "
                f"simulating anything and cannot fail")
        if not seen:
            problems.append(
                "the simulated-Windows open() observed no text-mode write at "
                "all, so it is not intercepting anything")

        # The real producer, on a simulated Windows host.
        written = {}
        for platform in sorted(module.LAUNCHERS):
            root = os.path.join(directory, platform)
            os.mkdir(root)
            filtered_writes = []
            real_open = builtins.open
            builtins.open = _windows_open(filtered_writes)
            try:
                with contextlib.redirect_stdout(io.StringIO()):
                    module.emit(root, platform)
            except (OSError, ValueError) as error:
                problems.append(
                    f"make_release_launchers.emit({platform!r}) failed on the "
                    f"simulated-Windows host: {error}")
                continue
            finally:
                builtins.open = real_open
            for name, _, _ in module.LAUNCHERS[platform]:
                with real_open(os.path.join(root, name), "rb") as handle:
                    written[name] = handle.read()

    for name, data in sorted(written.items()):
        problems.extend(contract.newline_problems(
            name, data,
            label=f"{name} emitted on a simulated Windows host"))

    # The constants themselves are well-formed, and the filter is known to
    # break them. Both matter: without the first, emit() could be writing
    # garbage that happens to be unbreakable; without the second, the check
    # above would pass on a filter that detects nothing.
    for name, text in ((module.POSIX_LAUNCHER_NAME, module.POSIX_LAUNCHER),
                       (module.WINDOWS_LAUNCHER_NAME, module.WINDOWS_LAUNCHER)):
        problems.extend(contract.newline_problems(
            name, text.encode("utf-8"), label=f"the {name} constant"))
        if not contract.newline_problems(
                name, contract.windows_text_mode_write(text).encode("utf-8")):
            problems.append(
                f"the {name} constant survives a Windows text-mode write; the "
                f"filter no longer models the defect this check guards "
                f"against")
    return problems


def check_emitted_bytes():
    """Run the helper for real and re-check the bytes it writes.

    The constants above can be correct while the writer is not: open() in
    text mode on a platform that translates newlines rewrites the CRLF the
    .bat depends on, and no static read of the constant would notice.
    """
    problems = []
    module = load_launcher_module()
    platforms = sorted(module.LAUNCHERS)
    with tempfile.TemporaryDirectory(prefix="rowl-launcher-parity-") as directory:
        for platform in platforms:
            root = os.path.join(directory, platform)
            os.mkdir(root)
            try:
                # emit() narrates on stdout; the gate's own report is the
                # only thing a CI log reader should have to parse.
                with contextlib.redirect_stdout(io.StringIO()):
                    module.emit(root, platform)
            except (OSError, ValueError) as error:
                problems.append(
                    f"make_release_launchers.emit({platform!r}) failed: {error}")
                continue
            present = {entry for entry in os.listdir(root)}
            wanted = {name for name, _, _ in module.LAUNCHERS[platform]}
            for name in sorted(present - wanted):
                problems.append(
                    f"--platform {platform} wrote unexpected file {name!r}")
            for missing in sorted(wanted - present):
                problems.append(
                    f"--platform {platform} did not write {missing!r}")
            for name in sorted(wanted & present):
                with open(os.path.join(root, name), "r", encoding="utf-8",
                          newline="") as handle:
                    problems.extend(contract.launcher_problems(
                        name, handle.read(), contract.PLAYER_NAMES,
                        full=True))
            # The POSIX launcher is invoked as an executable, not read: it
            # must carry the execute bit.
            posix_path = os.path.join(root, module.POSIX_LAUNCHER_NAME)
            if os.path.isfile(posix_path) and not os.access(posix_path, os.X_OK):
                problems.append(
                    f"{module.POSIX_LAUNCHER_NAME} is not executable; "
                    f"./run_game.sh would fail with permission denied")
    return problems


def decode_csharp_literal(text):
    """Decode a C# regular string literal body into its runtime characters."""
    out = []
    index = 0
    while index < len(text):
        char = text[index]
        if char == "\\" and index + 1 < len(text):
            index += 1
            escape = text[index]
            out.append({"n": "\n", "r": "\r", "t": "\t", "0": "\0",
                        "\\": "\\", '"': '"', "'": "'"}.get(escape,
                                                             "\\" + escape))
        else:
            out.append(char)
        index += 1
    return "".join(out)


def editor_launcher_literals():
    """The launcher text the editor build service writes, or None."""
    try:
        with open(EDITOR_BUILD_SERVICE, "r", encoding="utf-8") as handle:
            source = handle.read()
    except OSError:
        return None
    literals = {}
    for name in (contract.POSIX_LAUNCHER_NAME, contract.WINDOWS_LAUNCHER_NAME):
        marker = '"%s"' % name
        start = source.find(marker)
        if start < 0:
            return None
        # The marker is a path argument, e.g. Path.Combine(staging, "run_game.bat"),
        # so the launcher TEXT is the next string literal after the following
        # argument separator.
        comma = source.find(",", start + len(marker))
        if comma < 0:
            return None
        cursor = comma + 1
        while cursor < len(source) and source[cursor] in " \t":
            cursor += 1
        if cursor >= len(source) or source[cursor] != '"':
            return None
        cursor += 1
        # A C# literal cannot contain a bare `"`, so the closing quote of the
        # value is the first one the escape walk does not consume.
        body = []
        while cursor < len(source):
            char = source[cursor]
            if char == "\\" and cursor + 1 < len(source):
                body.append(source[cursor:cursor + 2])
                cursor += 2
                continue
            if char == '"':
                break
            body.append(char)
            cursor += 1
        if cursor >= len(source):
            return None
        literals[name] = decode_csharp_literal("".join(body))
    return literals


def check_editor_launcher_parity():
    """The editor emits the same launcher text; the two copies must agree."""
    literals = editor_launcher_literals()
    if literals is None:
        return [("editor/Services/ProjectBuildService.cs: could not locate the "
                 "launcher literals; the editor's copy of the launcher text is "
                 "no longer compared against the helper's")]
    module = load_launcher_module()
    problems = []
    expected = {
        contract.POSIX_LAUNCHER_NAME: module.POSIX_LAUNCHER,
        contract.WINDOWS_LAUNCHER_NAME: module.WINDOWS_LAUNCHER,
    }
    for name, text in sorted(literals.items()):
        problems.extend(contract.launcher_problems(name, text,
                                                   contract.PLAYER_NAMES,
                                                   full=True))
        if expected.get(name) != text:
            problems.append(
                f"editor/Services/ProjectBuildService.cs writes a different "
                f"{name} than tools/make_release_launchers.py; the editor "
                f"build and the CI package would ship different launchers")
    return problems


def main():
    try:
        checked, problems = check_workflow()
        problems.extend(check_launcher_contract())
        problems.extend(check_emitted_bytes())
        problems.extend(check_line_endings_are_host_independent())
        problems.extend(check_editor_launcher_parity())
    except (OSError, ValueError, ImportError) as error:
        print("[LauncherParity] ERROR: " + str(error), file=sys.stderr)
        return 1

    if problems:
        for problem in problems:
            fail(problem)
        print(f"[LauncherParity] {len(problems)} problem(s) across "
              f"{len(checked)} packaging step(s).", file=sys.stderr)
        return 1

    for title in checked:
        print(f"[LauncherParity] OK  {title}")
    print(f"[LauncherParity] {len(checked)} packaging step(s) emit a launcher "
          f"before verification; launcher names and content match the "
          f"verifier.")
    return 0


if __name__ == "__main__":
    sys.exit(main())