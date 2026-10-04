import test from 'node:test';
import assert from 'node:assert/strict';
import { initialPlayback, startPlayback, togglePause, advancePlayback, playbackFrame } from './playback-state.mjs';

const scene = () => ({ title: 'Gece', dialogue: 'Mira yola çıkıyor.', objects: { character: { x: 25, y: 35, w: 13, h: 36 } } });

test('Başlatma seçili sahnenin bağımsız kopyasını oynatır', () => {
  const author = scene(), playing = startPlayback(author);
  author.objects.character.x = 60; author.dialogue = 'Başka metin';
  assert.equal(playing.status, 'playing');
  assert.equal(playing.scene.objects.character.x, 25);
  assert.equal(playing.scene.dialogue, 'Mira yola çıkıyor.');
});
test('Çalışırken hareket ve yazı ilerler; duraklatınca kare aynı kalır', () => {
  const playing = advancePlayback(startPlayback(scene()), .4);
  const frame = playbackFrame(playing);
  assert.notEqual(frame.objects.character.x, 25);
  assert.equal(frame.dialogue, 'Mira yola');
  const paused = togglePause(playing);
  assert.equal(paused.status, 'paused');
  assert.equal(advancePlayback(paused, 100), paused);
  assert.deepEqual(playbackFrame(advancePlayback(paused, 100)), frame);
});
test('Devam etme donmuş kareden ilerler; duraklama süresi eklenmez', () => {
  const paused = togglePause(advancePlayback(startPlayback(scene()), .5));
  const resumed = togglePause(paused);
  assert.equal(resumed.elapsed, .5);
  assert.equal(advancePlayback(resumed, .1).elapsed, .6);
});
test('Kapatma ve yeniden başlatma oynatımı sıfırlar', () => {
  const stopped = initialPlayback();
  assert.equal(stopped.status, 'stopped');
  assert.equal(playbackFrame(stopped), null);
  assert.equal(togglePause(stopped), stopped);
  assert.equal(advancePlayback(stopped, 2), stopped);
  const restarted = startPlayback({ ...scene(), title: 'Yeni sahne' });
  assert.equal(restarted.elapsed, 0);
  assert.equal(restarted.scene.title, 'Yeni sahne');
});
test('Önizleme sınırların dışına çıkmaz ve düzenleme verisini değiştirmez', () => {
  const author = scene(); author.objects.character.x = 87; author.objects.character.y = 64;
  const original = structuredClone(author), playing = startPlayback(author);
  for (const seconds of [1, 10, 100]) {
    const frame = playbackFrame(advancePlayback(playing, seconds));
    assert.ok(frame.objects.character.x >= 0 && frame.objects.character.x <= 87);
    assert.ok(frame.objects.character.y >= 0 && frame.objects.character.y <= 64);
  }
  assert.deepEqual(author, original);
  for (const seconds of [NaN, Infinity, -1, 0]) assert.equal(advancePlayback(playing, seconds), playing);
});
