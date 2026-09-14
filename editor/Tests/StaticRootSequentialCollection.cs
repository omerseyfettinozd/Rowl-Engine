using Xunit;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Tests in this collection construct <c>MainWindowViewModel</c>, whose
/// constructor stamps the static project root that the headless suite's
/// save/load/validate flow reads. They run sequentially with each other
/// (never in parallel) so temp-project file operations cannot interleave.
/// </summary>
[CollectionDefinition("StaticRootSequential")]
public class StaticRootSequentialCollection
{
}
