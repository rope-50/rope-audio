// Flame demo of rope_audio: a looping music bed, tap-to-play SFX panned by
// screen position, an auto-firing SFX so it's lively without interaction, a
// master-volume slider, and engine events drained in the Flame update loop.

import 'dart:math';

import 'package:flame/components.dart';
import 'package:flame/effects.dart';
import 'package:flame/events.dart';
import 'package:flame/game.dart';
import 'package:flutter/material.dart';
import 'package:rope_audio/rope_flutter.dart';

void main() => runApp(const RopeExampleApp());

class RopeExampleApp extends StatelessWidget {
  const RopeExampleApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'rope_audio + Flame',
      debugShowCheckedModeBanner: false,
      theme: ThemeData.dark(useMaterial3: true),
      home: const GamePage(),
    );
  }
}

class GamePage extends StatefulWidget {
  const GamePage({super.key});

  @override
  State<GamePage> createState() => _GamePageState();
}

class _GamePageState extends State<GamePage> {
  late final RopeAudioGame _game = RopeAudioGame();
  double _master = 0.8;

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: Stack(
        children: [
          GameWidget(game: _game),
          Positioned(
            left: 16,
            top: 24,
            right: 16,
            child: ValueListenableBuilder<String>(
              valueListenable: _game.status,
              builder: (_, value, __) => Text(
                value,
                style: const TextStyle(fontSize: 14, color: Colors.white70),
              ),
            ),
          ),
          Positioned(
            left: 16,
            right: 16,
            bottom: 16,
            child: Row(
              children: [
                const Text('Master'),
                Expanded(
                  child: Slider(
                    value: _master,
                    onChanged: (v) {
                      setState(() => _master = v);
                      _game.setMaster(v);
                    },
                  ),
                ),
                Text(_master.toStringAsFixed(2)),
              ],
            ),
          ),
        ],
      ),
    );
  }
}

class RopeAudioGame extends FlameGame with TapCallbacks {
  final RopeAudio audio = RopeAudio();
  final ValueNotifier<String> status = ValueNotifier<String>('starting...');
  final Random _rng = Random();

  int _music = kInvalidSound;
  int _sfx = kInvalidSound;
  int _finished = 0;

  @override
  Future<void> onLoad() async {
    final ok = audio.start();
    audio.enableLifecycleHandling();

    _music = await audio.loadAsset('assets/tone_a4_mono.wav');
    _sfx = await audio.loadAsset('assets/chord_stereo.wav');
    audio.engine.masterVolume = 0.8;

    if (_music != kInvalidSound) {
      audio.engine.play(_music, gain: 0.25, loop: true);
    }

    // Auto-fire SFX every 1.2s with a random pan so the demo is audible even
    // without taps (handy for verification).
    add(TimerComponent(
      period: 1.2,
      repeat: true,
      onTick: () {
        if (_sfx != kInvalidSound) {
          audio.engine.play(_sfx, gain: 0.6, pan: _rng.nextDouble() * 2 - 1);
        }
      },
    ));

    status.value = ok
        ? 'rope ${audio.engine.sampleRate} Hz / ${audio.engine.channels} ch — tap to play (pan follows X)'
        : 'engine failed to start';
  }

  @override
  void onTapDown(TapDownEvent event) {
    final p = event.localPosition;
    final pan = RopeAudio.panForX(p.x, size.x);
    if (_sfx != kInvalidSound) {
      audio.engine.play(_sfx, gain: 0.7, pan: pan);
    }
    add(CircleComponent(
      radius: 18,
      position: p,
      anchor: Anchor.center,
      paint: Paint()..color = Colors.tealAccent.withValues(alpha: 0.85),
    )..add(RemoveEffect(delay: 0.4)));
  }

  @override
  void update(double dt) {
    super.update(dt);
    for (final e in audio.engine.pollEvents()) {
      if (e.type == RopeEventType.voiceFinished) _finished++;
      status.value = 'last event: ${e.type.name}   (voices finished: $_finished)';
    }
  }

  void setMaster(double v) => audio.engine.masterVolume = v;

  @override
  void onRemove() {
    audio.dispose();
    super.onRemove();
  }
}
