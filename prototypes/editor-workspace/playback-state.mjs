// Local preview only: authoring data stays separate from the running game.
export const initialPlayback = () => ({ status: 'stopped', elapsed: 0, scene: null });
export const startPlayback = scene => ({ status: 'playing', elapsed: 0, scene: structuredClone(scene) });
export function togglePause(playback) {
  if (playback.status === 'stopped') return playback;
  return { ...playback, status: playback.status === 'playing' ? 'paused' : 'playing' };
}
export function advancePlayback(playback, seconds) {
  if (playback.status !== 'playing' || !Number.isFinite(seconds) || seconds <= 0) return playback;
  return { ...playback, elapsed: playback.elapsed + seconds };
}
export function playbackFrame(playback) {
  if (!playback.scene) return null;
  const scene = structuredClone(playback.scene), character = scene.objects.character;
  character.x = Math.max(0, Math.min(100 - character.w, character.x + Math.sin(playback.elapsed * 1.2) * 3));
  character.y = Math.max(0, Math.min(100 - character.h, character.y + Math.sin(playback.elapsed * 2.4) * .8));
  scene.dialogue = Array.from(scene.dialogue).slice(0, Math.floor(playback.elapsed * 24)).join('');
  return scene;
}
