// Headless smoke test for the C# bindings. Mirrors the Dart/C smoke tests:
// it drives the real native library through the Null backend and offline
// rendering, so it needs no audio hardware and produces deterministic output.
//
// Run after copying the native rope_audio library next to the built exe:
//   dotnet run --project RopeAudio.Smoke
// (See README for how to build/copy the native library.)

using System;
using System.IO;
using Rope;

static class Program
{
    static int Main()
    {
        try
        {
            Console.WriteLine($"rope ABI: 0x{RopeEngine.AbiVersion:x8}");

            using var engine = new RopeEngine();
            if (!engine.Start(backend: RopeBackend.Null))
            {
                Console.Error.WriteLine("FAIL: engine.Start(Null) returned false");
                return 1;
            }
            Console.WriteLine($"engine: {engine.SampleRate} Hz, {engine.Channels} ch");

            // A deterministic constant-amplitude mono WAV decoded from memory.
            const float amp = 0.5f;
            const int frames = 4000;
            byte[] wav = MakeWavPcm16(channels: 1, sampleRate: 48000, Const(amp, frames));
            uint sound = engine.LoadWavBytes(wav);
            if (sound == RopeHandle.InvalidSound)
            {
                Console.Error.WriteLine("FAIL: LoadWavBytes returned invalid handle");
                return 1;
            }
            Console.WriteLine($"loaded {wav.Length} bytes -> sound {sound}");

            // master 0.8 * gain 0.5 * panL 1.0 (hard left) * src 0.5 = 0.2 on L, 0 on R.
            engine.MasterVolume = 0.8f;
            ulong voice = engine.Play(sound, gain: 0.5f, pan: -1f);
            Console.WriteLine($"play -> voice {voice}");

            // Render past the master smoothing ramp (~5 ms) and check a settled frame.
            var buf = new float[512 * 2];
            engine.RenderOffline(buf, 512);
            float l = buf[500 * 2], r = buf[500 * 2 + 1];
            Console.WriteLine($"settled frame: L={l:F4} R={r:F4} (expect L~0.20 R~0.00)");
            if (!Near(l, 0.2f, 0.02f) || !Near(r, 0.0f, 0.001f))
            {
                Console.Error.WriteLine("FAIL: rendered output off expected value");
                return 1;
            }

            // Fade out and confirm the voice finishes within the fade window.
            engine.StopVoice(voice, fadeOut: 0.01f); // 10 ms = 480 frames
            bool finished = false;
            for (int i = 0; i < 32 && !finished; i++)
            {
                engine.RenderOffline(buf, 64);
                while (engine.PollEvent(out var ev))
                {
                    Console.WriteLine($"[event] {ev.Type} voice={ev.Voice} reason={ev.Reason}");
                    if (ev.Type == RopeEventType.VoiceFinished && ev.Voice == voice)
                        finished = true;
                }
            }
            if (!finished)
            {
                Console.Error.WriteLine("FAIL: voice did not finish after fade-out");
                return 1;
            }

            engine.Stop();
            Console.WriteLine("done");
            return 0;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"FAIL: {ex}");
            return 1;
        }
    }

    static float[] Const(float v, int frames)
    {
        var s = new float[frames];
        for (int i = 0; i < frames; i++) s[i] = v;
        return s;
    }

    static bool Near(float a, float b, float eps) => Math.Abs(a - b) <= eps;

    // Minimal 16-bit PCM WAV writer (interleaved float samples in [-1, 1]).
    static byte[] MakeWavPcm16(ushort channels, uint sampleRate, float[] interleaved)
    {
        ushort bits = 16;
        ushort blockAlign = (ushort)(channels * bits / 8);
        uint byteRate = sampleRate * blockAlign;
        uint dataSize = (uint)(interleaved.Length * sizeof(short));

        using var ms = new MemoryStream();
        using var w = new BinaryWriter(ms);
        w.Write(System.Text.Encoding.ASCII.GetBytes("RIFF"));
        w.Write(36u + dataSize);
        w.Write(System.Text.Encoding.ASCII.GetBytes("WAVE"));
        w.Write(System.Text.Encoding.ASCII.GetBytes("fmt "));
        w.Write(16u);                 // fmt chunk size
        w.Write((ushort)1);           // PCM
        w.Write(channels);
        w.Write(sampleRate);
        w.Write(byteRate);
        w.Write(blockAlign);
        w.Write(bits);
        w.Write(System.Text.Encoding.ASCII.GetBytes("data"));
        w.Write(dataSize);
        foreach (float f in interleaved)
        {
            float c = Math.Clamp(f, -1f, 1f);
            w.Write((short)Math.Round(c * 32767f));
        }
        w.Flush();
        return ms.ToArray();
    }
}
