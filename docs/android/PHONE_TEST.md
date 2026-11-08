# First run on a real phone

About 20 minutes. Everything before this was tested on the emulator with a fake Seaboard
(`tools/emulator_test.py`, 43 checks), so this is about the parts an emulator cannot show:
the real USB and Bluetooth paths, the phone's audio latency, and how it feels to play.

You need: the phone, a USB-C to USB-C cable **that carries data** (one you have used to copy
files), and the Seaboard charged. A USB-C OTG adapter only if the plain cable does not work.

## 1. Once: USB debugging

Settings → About phone → tap **Build number** 7 times. Then Settings → System → Developer
options → **USB debugging** on.

## 2. Phone plugged into the computer

    tools/phone.sh check        # what the phone offers (low-latency flag, MMAP policy)
    tools/phone.sh install      # app + fake Seaboard
    tools/phone.sh fake-test    # the emulator suite, on the phone (it will make sound)

`fake-test` checks the whole app on the phone's real audio hardware before the Seaboard is
involved. If something fails here, it is the phone's audio, not the controller.

    tools/phone.sh report       # saves the numbers to captures/

## 3. Seaboard over USB

Unplug the phone from the computer. Plug the Seaboard into the phone. Open the app.

- It should say **Connected: Seaboard BLOCK** by itself, within a second or two.
- Play. Try every instrument for a moment, chords, slides, pressure.
- Pull the cable mid-note: the sound must stop.
- Screenshot the "Latency and load" section while playing.

If it never says Connected: try the OTG adapter. If the app does not list the Seaboard at all,
the cable is probably charge-only.

## 4. Seaboard over Bluetooth

Turn the Seaboard's Bluetooth on (see its manual). In the app: **Scan for Bluetooth LE
controllers**, then tap the Seaboard. Leave the watchdog on **auto** (1500 ms on Bluetooth).
Play, and screenshot the numbers again. Walk away with the Seaboard until the link drops: the
sound must stop within about 1.5 s, and the app should say it is waiting for the Seaboard.
Walk back: it should reconnect by itself. (On the emulator Android took about 30 s to report a
lost Bluetooth link; the watchdog is what stops the sound sooner.)

## 5. Finger to sound, measured from outside

The app measures from "Android received the note" to "sound leaves the buffer". It cannot see
the Seaboard's own scan time or the USB/Bluetooth transfer. To get the real figure:

1. Instrument **struck / Marimba** (a sharp attack is easy to see).
2. A second phone filming in slow motion (240 fps if possible), close to the Seaboard and to
   the playing phone's speaker.
3. Tap one key firmly, about 10 times, a second apart.
4. Do it over USB, then over Bluetooth. If you can, also with the Seaboard on a Mac or iPad
   playing a similar sound: that is the comparison that answers "is Android slower?".

Send the videos; the gap between the finger landing and the sound is counted frame by frame
(at 240 fps one frame is about 4 ms).

## 6. Tilt and breath

The emulator checks that both reach the sound (a turn bends by the right amount, the right way
round; breath silences and sounds a held note). What it cannot show: how they feel, and how the
phone's own speaker and microphone get on. Setup tab, section **Experimental**. Once one is on, it
also appears on the touch pad's side rail.

**Tilt.** Choose **Bend**. Open the touch pad, hold a note and turn the phone like a steering
wheel: clockwise bends up, and the little phone at the top turns with you. Let go, hold the phone
at a slant, play again: the new note must start in tune. The screen must not flip round while
tilt is on. Then **Vibrato**: wobble the phone while holding a note; holding it still at a slant
must stay in tune. Is the depth right at the default 40 cents? Does pressing the screen with your
thumb wobble the pitch by itself? Then **Tone**.

**Breath.** Choose **Swell** and allow the microphone. On the pad, hold a note: silence. Blow
across the bottom edge of the phone: the note swells, and the meter on the Play tab (or the
pad's Breath panel) fills. Note the two numbers under the meter, "room" and "starts".

1. **Does it come and go when you want?** Short puffs, a long steady breath (listen for a
   flutter in the loudness), and the moment you stop (it should die away within a fifth of a
   second or so).
2. **The speaker test, the important one.** Phone speaker at full volume, no headphones,
   instrument **Bass** or **Cello**. Play low notes, blow once, stop. The sound must die away. If it
   carries on by itself, the app is hearing its own sound as breath: screenshot the meter (the
   red tick is the app's own sound, which breath has to beat) and say so. The detector assumes
   the speaker reaches the microphone at most 24 dB down; that number may need changing.
3. **Latency.** Write down the "ms" in the status line with breath off, then on. Opening the
   microphone can make Android choose a slower output path.
4. **Screen off.** With the Seaboard over USB and breath on, turn the screen off and blow: it must
   still work (the green microphone dot stays on).
5. **Sensitivity.** Too hard to start a note, or does talking or a fan set it off? Try the slider.

## 7. Updating from the app

0.5.0 is the first version that can update itself, so install it by hand once. When the next
release is out, the Play tab says so. Setup → Updates → **Update to …**. The first time,
Android sends you to "Install unknown apps" for odo: turn it on and come back, and the
install carries on. Xiaomi phones may show their own scan screen first. The app closes while
Android replaces it; reopen it and check the version at the bottom of Setup. Your presets and
settings must still be there.

## What to send back

- The `captures/phone-*.txt` file from step 2 (it is saved in the repo folder).
- Screenshots from steps 3 and 4.
- Anything that felt wrong: notes that hang, late or uneven notes, crackles.
- The videos from step 5.
- From step 6: the "room" and "starts" numbers, the latency with breath off and on, and whether
  the speaker test passed.
