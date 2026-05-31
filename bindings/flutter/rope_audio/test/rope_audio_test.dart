import 'package:flutter_test/flutter_test.dart';
import 'package:rope_audio/rope_flutter.dart';

void main() {
  group('RopeAudio.panForX', () {
    test('maps screen x to a [-1, 1] pan', () {
      expect(RopeAudio.panForX(0, 100), -1.0);
      expect(RopeAudio.panForX(50, 100), 0.0);
      expect(RopeAudio.panForX(100, 100), 1.0);
    });

    test('clamps out-of-range x and handles zero width', () {
      expect(RopeAudio.panForX(-10, 100), -1.0);
      expect(RopeAudio.panForX(200, 100), 1.0);
      expect(RopeAudio.panForX(5, 0), 0.0); // width 0 -> center
    });
  });

  test('invalid handle constants match the C ABI', () {
    expect(kInvalidSound, 0xFFFFFFFF);
    expect(kInvalidVoice, 0);
  });
}
