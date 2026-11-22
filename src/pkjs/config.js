// Clay settings page for Ignition.
module.exports = [
  { type: 'heading', defaultValue: 'Ignition' },
  {
    type: 'text',
    defaultValue: 'The engines tell the time. Top center engine: lit for AM, dark for PM. The other 12 inner engines: the hour (that many lit). The outer 20: minutes, one engine per 3 minutes; the white-hot one is the newest. The rim is a mission timeline: filled nodes have passed, the haloed node is now.'
  },
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Rim' },
      {
        type: 'select',
        messageKey: 'RimMode',
        label: 'Default rim',
        defaultValue: '3',
        options: [
          { label: 'Hours', value: '0' },
          { label: 'Steps', value: '1' },
          { label: 'Day timeline', value: '2' },
          { label: 'Month', value: '3' },
          { label: 'T-minus countdown', value: '4' },
          { label: 'Moon', value: '5' },
          { label: 'Moon & Mars distance', value: '6' },
          { label: 'Seconds', value: '7' }
        ]
      },
      {
        type: 'toggle',
        messageKey: 'FlickSwap',
        label: 'Flick wrist for complications',
        defaultValue: true
      },
      {
        type: 'select',
        messageKey: 'FlickSeconds',
        label: 'Flick lasts',
        description: 'How long a flick keeps the other complication up before the rim returns to the default.',
        defaultValue: '8',
        options: [
          { label: '5 seconds', value: '5' },
          { label: '8 seconds', value: '8' },
          { label: '10 seconds', value: '10' }
        ]
      },
      {
        type: 'toggle',
        messageKey: 'TapScreen',
        label: 'Tap screen for complications',
        description: 'Pebble Round 2 only. A quick tap does the same as a flick. The touch sensor stays powered while this is on; turn it off if you notice extra battery drain.',
        defaultValue: true
      },
      {
        type: 'select',
        messageKey: 'FlickMode',
        label: 'Flick shows',
        description: 'Cycle: each flick shows the next complication turned on below. One: a flick shows the complication picked here; flick again to go back.',
        defaultValue: '0',
        options: [
          { label: 'Cycle through complications', value: '0' },
          { label: 'One complication', value: '1' }
        ]
      },
      {
        type: 'select',
        messageKey: 'FlickTarget',
        label: 'Complication to show',
        description: 'Used when Flick shows is set to One. Pick something other than the default rim.',
        defaultValue: '1',
        options: [
          { label: 'Hours', value: '0' },
          { label: 'Steps', value: '1' },
          { label: 'Day timeline', value: '2' },
          { label: 'Month', value: '3' },
          { label: 'T-minus countdown', value: '4' },
          { label: 'Moon', value: '5' },
          { label: 'Moon & Mars distance', value: '6' },
          { label: 'Seconds', value: '7' }
        ]
      },
      {
        type: 'toggle',
        messageKey: 'FlickRearrange',
        label: 'Flick also rearranges engines',
        description: 'While a flick is up, the engines switch between the startup sequence and the clock-like dial arrangement, then switch back with the rim. For rearranging alone, set Flick shows to One, pick your default rim and turn Show date off.',
        defaultValue: false
      },
      {
        type: 'toggle',
        messageKey: 'ShowDate',
        label: 'Show date on flick',
        description: 'A broadcast-style readout across the engines: day and date, and the day of the year. To have a flick show only the date, set Flick shows to One and pick your default rim.',
        defaultValue: true
      }
    ]
  },
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Complications' },
      {
        type: 'text',
        defaultValue: 'Hours (12 white nodes, the current hour haloed) is always available, though the engines already show it.'
      },
      {
        type: 'toggle',
        messageKey: 'ShowSteps',
        label: 'Steps',
        description: '10 flame-colored nodes, 10% of your goal each. The ring closes at the goal.',
        defaultValue: true
      },
      {
        type: 'input',
        messageKey: 'StepGoal',
        label: 'Daily step goal',
        defaultValue: '8000',
        attributes: { type: 'number', min: 1000, max: 50000, step: 500 }
      },
      {
        type: 'toggle',
        messageKey: 'ShowDay',
        label: 'Day timeline',
        description: 'Sky blue. The whole day with midnight at the top; nodes mark sunrise, solar noon and sunset for your location.',
        defaultValue: true
      },
      {
        type: 'toggle',
        messageKey: 'ShowMonth',
        label: 'Month',
        description: 'Pale yellow. One node per day of the month, starting at the top; today is haloed.',
        defaultValue: true
      },
      {
        type: 'toggle',
        messageKey: 'ShowMoon',
        label: 'Moon',
        description: 'Moonlight. One lunar cycle with new moon at the top; nodes mark first quarter, full and last quarter. The marker shows tonight\'s phase. Mirrored south of the equator.',
        defaultValue: true
      },
      {
        type: 'toggle',
        messageKey: 'ShowOrbits',
        label: 'Moon & Mars distance',
        description: 'How far each is from Earth. 12 o\'clock (the blue Earth node) is closest, 6 o\'clock farthest, scaled to each one\'s range: Moon 356,400-406,700 km, Mars 0.37-2.68 AU. Left side = getting closer, right side = moving away.',
        defaultValue: true
      },
      {
        type: 'toggle',
        messageKey: 'ShowSeconds',
        label: 'Seconds',
        description: 'Mint green. A seconds hand travelling the rim, a node every 5 seconds. The face redraws every second while this is showing, so as the default rim it uses noticeably more battery; as a flick complication it costs very little.',
        defaultValue: true
      },
      {
        type: 'toggle',
        messageKey: 'ShowTMinus',
        label: 'T-minus countdown',
        description: 'Red-orange. Until the last 30 days, 10 nodes show how much of the wait has passed since you set the date. Then one node per day until the halo reaches 12 on the day.',
        defaultValue: false
      },
      {
        type: 'input',
        messageKey: 'TMinusDate',
        label: 'Countdown date',
        defaultValue: '',
        attributes: { type: 'date' }
      }
    ]
  },
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Engines' },
      {
        type: 'select',
        messageKey: 'EngineOrder',
        label: 'Engine order',
        description: 'Dial lights the hour and minute engines clockwise, so they read like a clock (5 outer engines = :15). Startup lights them in opposite pairs like an engine startup; count the lit engines to read them. Opening the face always plays the startup sequence.',
        defaultValue: '0',
        options: [
          { label: 'Dial (reads like a clock)', value: '0' },
          { label: 'Startup sequence (count them)', value: '1' }
        ]
      },
      {
        type: 'toggle',
        messageKey: 'PulseMinutes',
        label: 'Pulse the current minute',
        description: 'Each minute engine covers 3 minutes: it pulses slowly in the first, faster in the second, and is lit in the third. Uses more battery, since the screen keeps redrawing that engine.',
        defaultValue: false
      },
      {
        type: 'select',
        messageKey: 'Flame',
        label: 'Flame',
        defaultValue: '0',
        options: [
          { label: 'Orange', value: '0' },
          { label: 'Methane (blue-violet)', value: '1' },
          { label: 'White', value: '2' }
        ]
      }
    ]
  },
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Alerts' },
      { type: 'toggle', messageKey: 'VibeDisconnect', label: 'Vibrate when phone disconnects', defaultValue: true }
    ]
  },
  { type: 'submit', defaultValue: 'Save' }
];
