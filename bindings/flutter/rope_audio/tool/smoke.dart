// Headless smoke test of the Dart FFI bindings against the native rope DLL.
//
//   dart run tool/smoke.dart <path-to-rope_audio.dll> <assets-dir>
//
// Loads one WAV from a file and one from memory, plays them panned with a
// master volume, and drains engine events for a few seconds.

import 'dart:io';

import 'package:rope_audio/rope_audio.dart';

void main(List<String> args) {
  final dllPath = args.isNotEmpty ? args[0] : 'rope_audio.dll';
  final assetsDir = args.length > 1 ? args[1] : 'assets';

  final engine = RopeEngine(libraryPath: dllPath);
  stdout.writeln(
      'rope ABI: 0x${engine.abiVersion.toRadixString(16).padLeft(8, '0')}');

  if (!engine.start()) {
    stderr.writeln('engine start failed');
    engine.dispose();
    exit(1);
  }
  stdout.writeln('engine: ${engine.sampleRate} Hz, ${engine.channels} ch');

  // One from file, one from memory (exercises load_wav_memory).
  final filePath = '$assetsDir/tone_a4_mono.wav';
  final memPath = '$assetsDir/chord_stereo.wav';

  final sFile = engine.loadFile(filePath);
  stdout.writeln('load file   "$filePath" -> sound $sFile');

  final bytes = File(memPath).readAsBytesSync();
  final sMem = engine.loadBytes(bytes);
  stdout.writeln('load memory "$memPath" (${bytes.length} bytes) -> sound $sMem');

  engine.masterVolume = 0.8;

  if (sFile != kInvalidSound) engine.play(sFile, gain: 0.7, pan: -1.0);
  if (sMem != kInvalidSound) engine.play(sMem, gain: 0.7, pan: 1.0);

  stdout.writeln('playing ~4s, polling events...');
  for (var i = 0; i < 80; i++) {
    for (final e in engine.pollEvents()) {
      stdout.writeln('[event] $e');
    }
    sleep(const Duration(milliseconds: 50));
  }

  engine.dispose();
  stdout.writeln('done');
}
