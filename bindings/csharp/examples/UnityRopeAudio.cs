// Unity integration example for rope-audioengine (NOT compiled by the binding
// solution — drop it into a Unity project alongside Native.cs / RopeEngine.cs).
//
// Lifecycle: create the engine in Awake, start the device, drain events every
// Update, and dispose in OnDestroy. Audio runs on a native real-time thread and
// never calls back into managed code, so polling is all you need.
#if UNITY_2020_1_OR_NEWER
using UnityEngine;
using Rope;

public class UnityRopeAudio : MonoBehaviour
{
    [Tooltip("A .wav imported as a TextAsset (rename to .bytes, or use an addressable).")]
    public TextAsset clip;

    private RopeEngine _engine;
    private uint _sound = RopeHandle.InvalidSound;

    void Awake()
    {
        _engine = new RopeEngine();
        if (!_engine.Start())                       // default device + backend
        {
            Debug.LogError("rope: failed to start audio engine");
            return;
        }
        if (clip != null)
            _sound = _engine.LoadWavBytes(clip.bytes);
    }

    /// <summary>Play the clip panned by a world X position (-1 left .. +1 right).</summary>
    public ulong PlayAt(float panMinus1To1, float gain = 1f)
    {
        if (_sound == RopeHandle.InvalidSound) return RopeHandle.InvalidVoice;
        return _engine.Play(_sound, gain: gain, pan: Mathf.Clamp(panMinus1To1, -1f, 1f),
                            fadeIn: 0.02f);
    }

    void Update()
    {
        if (_engine == null) return;
        while (_engine.PollEvent(out RopeEvent ev))
        {
            switch (ev.Type)
            {
                case RopeEventType.VoiceFinished:
                    // e.g. recycle a pooled emitter keyed by ev.Voice
                    break;
                case RopeEventType.VoicesExhausted:
                    Debug.LogWarning("rope: voice pool exhausted");
                    break;
            }
        }
    }

    void OnApplicationPause(bool paused)
    {
        if (_engine == null) return;
        if (paused) _engine.Suspend(); else _engine.Resume();
    }

    void OnDestroy() => _engine?.Dispose();
}
#endif
