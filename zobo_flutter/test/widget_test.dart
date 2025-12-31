import 'package:flutter_test/flutter_test.dart';

import 'package:zobo_flutter/main.dart';

void main() {
  testWidgets('App loads successfully', (WidgetTester tester) async {
    // Build our app and trigger a frame.
    await tester.pumpWidget(const ZoboApp());

    // Verify that our app title is displayed.
    expect(find.text('Zobo Controller'), findsOneWidget);
  });
}
