// Godot .NET (C#) integration example for rope-audioengine (NOT compiled by the
// binding solution — add it to a Godot C# project alongside Native.cs /
// RopeEngine.cs).
//
// Lifecycle: create + start in _Ready, drain events in _Process, dispose in
// _ExitTree. Audio runs on a native real-time thread and never calls back into
// managed code, so polling is all you need.
#if GODOT
using Godot;
using Rope;

public partial class GodotRopeAudio : Node
{
    [Export] public string ClipPath = "res://sfx/shot.wav";

    private RopeEngine _engine;
    private uint _sound = RopeHandle.InvalidSound;

    public override void _Ready()
    {
        _engine = new RopeEngine();
        if (!_engine.Start())                       // default device + backend
        {
            GD.PushError("rope: failed to start audio engine");
            return;
        }
        byte[] bytes = FileAccess.GetFileAsBytes(ClipPath);
        if (bytes != null && bytes.Length > 0)
            _sound = _engine.LoadWavBytes(bytes);
    }

    /// <summary>Play the clip panned (-1 left .. +1 right).</summary>
    public ulong PlayAt(float panMinus1To1, float gain = 1f)
    {
        if (_sound == RopeHandle.InvalidSound) return RopeHandle.InvalidVoice;
        return _engine.Play(_sound, gain: gain, pan: Mathf.Clamp(panMinus1To1, -1f, 1f),
                            fadeIn: 0.02f);
    }

    public override void _Process(double delta)
    {
        if (_engine == null) return;
        while (_engine.PollEvent(out RopeEvent ev))
        {
            switch (ev.Type)
            {
                case RopeEventType.VoiceFinished:
                    // e.g. free a pooled emitter keyed by ev.Voice
                    break;
                case RopeEventType.VoicesExhausted:
                    GD.PushWarning("rope: voice pool exhausted");
                    break;
            }
        }
    }

    public override void _Notification(int what)
    {
        if (_engine == null) return;
        if (what == NotificationApplicationPaused) _engine.Suspend();
        else if (what == NotificationApplicationResumed) _engine.Resume();
    }

    public override void _ExitTree() => _engine?.Dispose();
}
#endif
