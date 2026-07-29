# 🔬 SUB-SPEC 05: DUAL-PATH AUDIO ENGINE & DSP FILTERS

> **Subsystem Target:** Dual-Path Audio Architecture (Streaming + RAM Pool), Audio Bus Hierarchy, Automatic Voice Ducking, DSP Environment Filters, and Voice-Typewriter Sync.

---

## 1. ARCHITECTURAL OVERVIEW

Audio creates the emotional atmosphere of a Visual Novel. Rowl Engine's audio subsystem is engineered for zero-latency UI sounds, low-RAM background music streaming, and real-time DSP (Digital Signal Processing) environmental effects.

```
                           +----------------------------------+
                           |       Master Audio Bus (0 dB)    |
                           +----------------------------------+
                                            │
         ┌─────────────────────────┬────────┴─────────────────────────┐
         ▼                         ▼                                  ▼
 [ BGM Bus (-3 dB) ]      [ Voice Bus (0 dB) ]               [ SFX Bus (0 dB) ]
  (Disk Streaming)         (VFS Streaming)                    (RAM Buffer Pool)
         │                         │                                  │
  Auto-Ducking Filter <──── Voice Signal Active ────              Zero Latency
 (Attenuates -6 dB)        Audio-Text Sync                        Instant Play
         │                         │                                  │
         └─────────────────────────┴──────────────────────────────────┘
                                   │
                                   ▼
                   [ DSP Environment Filter Node ]
             (Reverb / Low-Pass / Telephone / Underwater)
                                   │
                                   ▼
                      [ Hardware Output Device ]
```

---

## 2. DUAL-PATH AUDIO PIPELINE

To respect the *"Tost Makinesi"* zero-waste memory principle:

1. **Streaming Path (BGM & Long Voice-Overs):**
   - Format: Ogg Vorbis / Opus.
   - Mechanism: Audio tracks are NOT loaded into RAM entirely. A dedicated audio thread streams 256 KB ring buffers continuously from disk or VFS packages.
   - Result: 50 MB background music tracks consume only **< 1 MB RAM**.
2. **Memory Buffer Pool (SFX & UI Clicks):**
   - Format: Uncompressed PCM WAV or short Ogg Vorbis.
   - Mechanism: Frequently triggered sound effects (button clicks, page turns, screen shakes) are pre-decoded and kept in a high-priority RAM buffer pool.
   - Result: **Zero latency** playback when the user interacts with the UI.

---

## 3. AUDIO BUS HIERARCHY & AUTOMATIC DUCKING

- **Bus Structure:** `Master` -> `BGM`, `Voice`, `SFX`, `UI`. Independent volume control and muting per channel.
- **Automatic Ducking (Ducking Curve):**
  - When a Voice-Over clip starts playing on the `Voice Bus`, the engine automatically attenuates the `BGM Bus` volume by **-6 dB** (configurable) using a smooth S-curve fade over 200 ms.
  - When the Voice clip finishes, the BGM volume smoothly restores to its original level over 400 ms.
  - Ensures character dialogue is always crisp and intelligible over loud background music.

---

## 4. DSP ENVIRONMENTAL FILTERS

Creators can apply real-time DSP filters to any scene or dialogue node directly from the Editor Inspector without re-editing original audio files.

| Filter Preset | DSP Effect Applied | Cinematic Use Case |
| :--- | :--- | :--- |
| **Normal** | Direct Pass-Through (No Filter) | Standard dialogue and outdoors. |
| **Cave / Cathedral** | High-Density Reverb & Delay | Caves, large halls, empty rooms. |
| **Telephone / Radio** | Band-Pass Filter (300Hz - 3400Hz) | Phone calls, intercoms, radio chatter. |
| **Underwater / Muffled**| Low-Pass Filter (Cutoff at 800Hz) | Submerged scenes, outer-room muffled sound. |
| **Ethereal / Dream** | Pitch Shift + Chorus + Slow Reverb | Flashbacks, dream sequences, magic events. |

---

## 5. VOICE-TYPEWRITER SYNCHRONIZATION

- **Synchronization Lock:** When a dialogue node contains both a Voice-Over audio clip and typewriter text:
  1. The engine calculates the precise duration of the audio clip ($T_{\text{audio}}$).
  2. The typewriter text animation speed ($S_{\text{char}}$) is dynamically adjusted so that the final character appears on screen **exactly as the voice-over finishes speaking**.
- **Auto-Advance Hook:** If "Auto-Play" mode is enabled, the scene transitions to the next node automatically after a user-defined delay (e.g., 1.5 seconds) once the voice and text complete.
