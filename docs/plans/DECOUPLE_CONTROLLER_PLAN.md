The next release will be a big change. We want to change what the app is. The app should eventually be a platform where you can bring your own instruments(sounds) and controllers. Currenlty we have one controller the touch pad, but we want the ability to download more controllers and instruments.  
  
For this release we should simply seperate out the touch pad from the app, it will still remain in the app as a built in controller.   
  
  
Features that apply to just the touchpad:

- size
- octave
- key
- scale
- up/down
- glide
- look
- lock 
- split (each controller divides its own layout: the touch pad's two pads, the Grid's sections)

Features that stay app-wide (modifiers, see VISION_PLAN.md decision 7), so they work with
every controller, the Seaboard included:

- arp (each part's, saved with the sound)
- layer
- pitch assist, tilt, breath

A controller can say, for each modifier, on, off, or "I do this myself" (for example its own
arp), plus its defaults. The touch pad leaves them all on.

The side menu should have a option on top to switch controller, the option after that should be controller specific options, these features should move there. if we switch controller it will have its own unique set of options.  
  
presets should also save what controller is being used.  
  
So this app will have the abulity to switch controllers and instruments and bring its own features to each such as gyro, effects, loop etc

Status (2026-11-06): done. The controller interface came in 0.16; the arp moved to the app
after 0.20 (not yet released).
