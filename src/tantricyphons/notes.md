## Hardware

| Peripheral     | Hardware handling  | CPU involvement                 |
| -------------- | ------------------ | ------------------------------- |
| MCP4821 DAC    | PIO + DMA          | Fill next audio buffer          |
| MCP3208 ADC    | PIO                | Queue scans and collect results |
| LCD Hardware   | SPI + DMA          | Prepare changed display regions |
| Pico ADC0–2    | Internal ADC       | Start and collect conversions   |
| Gate input     | GPIO polling       | Track active-low gate state     |
| Rotary encoder | PIO                | Read accumulated movement       |
| Buttons        | GPIO               | Poll at UI refresh rate         |

## Pico 2 GPIO Allocation

| GPIO        | Assignment          | Peripheral     |
| ----------- | ------------------- | -------------- |
| GP0         | Debug probe TX      | UART0 TX       |
| GP1         | LCD CS              | SPI0           |
| GP2         | LCD SCK             | SPI0           |
| GP3         | LCD MOSI            | SPI0           |
| GP4         | LCD D/C             | LCD            |
| GP5         | LCD RESET           | LCD            |
| GP6         | LCD backlight PWM   | LCD            |
| GP7         | ADC CS              | MCP3208 PIO    |
| GP8         | ADC SCK             | MCP3208 PIO    |
| GP9         | ADC MOSI            | MCP3208 PIO    |
| GP10        | ADC DOUT            | MCP3208 PIO    |
| GP11        | DAC CS              | MCP4821 PIO    |
| GP12        | DAC SCK             | MCP4821 PIO    |
| GP13        | DAC MOSI            | MCP4821 PIO    |
| GP14        | DAC LDAC            | MCP4821 PIO    |
| GP15        | Unused              | —              |
| GP16        | Encoder A           | PIO            |
| GP17        | Encoder B           | PIO            |
| GP18        | Encoder push        | GPIO           |
| GP19        | Back/Cancel         | GPIO           |
| GP20        | Gate input          | GPIO polling   |
| GP21        | MIDI IN (pin 27)    | UART1 RX       |
| GP22        | Spare               | —              |
| GP26 / ADC0 | FREQ_CV             | Internal ADC   |
| GP27 / ADC1 | VOWEL_CV            | Internal ADC   |
| GP28 / ADC2 | VOWEL_POT           | Internal ADC   |

### Other Connections

| Pico 2 Pin                 | Assignment                   |
| -------------------------- | ---------------------------- |
| ADC_VREF (physical pin 35) | LM4040C30ILPR 3.0V reference |
| AGND (physical pin 33)     | Analog ground                |

## MIDI input

TRS MIDI is received on GP21 (physical Pico 2 pin 27), using UART1 at
31250 baud, 8 data bits, no parity, and 1 stop bit. This expects the decoded
logic signal from the board's MIDI input receiver at GP21.

`MIDI_CHANNEL` in `midi.h` selects the one-based channel (default 1).
Core 0 polls the UART; keep the UI loop responsive, servicing it well within
10 ms so the 32-byte FIFO does not overflow. Events cross the messenger queue
and update the voice on core 1 at audio buffer boundaries.

- Notes use last-note priority; releasing the newest held note restores the
  previous one. Note On with velocity zero acts as Note Off.
- Pitch wheel controls vowel (classic Monksynth mapping).
- CC1 controls vibrato, CC5 glide, CC7 level, and CC13 voice character.
- CC120, CC123, and System Reset release all held notes. Queue overflow or
  UART receive errors also release notes to recover from missing Note Offs.
- CC12 delay is currently unsupported because this project has no delay engine.
- Velocity is delivered to the voice API, which currently ignores it.
- Sustain pedal and active sensing timeouts are not implemented.

ADC acquisition publishes unassigned raw readings. Arbitration between CV
gate/pitch and MIDI remains to be defined when parameter routing is added.
MIDI source tags give controller updates temporary ownership over ADC updates.

Host parser check (no Pico hardware required):

```sh
cc -std=c11 -Wall -Wextra -Werror -Iinclude src/midi/midi.c tests/test_midi.c -o /tmp/tantricyphons-test-midi
/tmp/tantricyphons-test-midi
```

## DAC playback

The MCP4821 uses GP11 CS, GP12 SCK, GP13 SDI, and GP14 LDAC.
GP10 belongs exclusively to MCP3208 DOUT.

PIO0 uses one claimed state machine to send 16-bit commands MSB first and
pulse LDAC once per sample. Each frame takes 256 PIO cycles. The divider is
rounded to the nearest 1/256 to target 44.1 kHz from the current system clock;
at the default 150 MHz this gives approximately 44104.68 samples/second
(+106.0 ppm). Do not change clk_sys after initializing playback.

Two 256-sample buffers alternate through FREE → FILLING → READY → PLAYING.
Core 1 acquires a free buffer, renders samples and packs the DAC commands,
then submits it. Both initial buffers are filled before playback starts.
A PIO TX DREQ paces a single DMA channel. DMA IRQ1 on core 1 releases the
completed RAM buffer and starts the next ready buffer; the joined eight-word
TX FIFO bridges the interrupt handoff. A buffer is safe to refill as soon as
DMA has copied it, even while its final samples are still in the PIO FIFO.

This gives about 5.8 ms to render each block. If rendering or interrupt
handling misses its deadline, the PIO finishes its queued samples and stalls
with CS and LDAC high, holding the last output. Submitting the next buffer
resumes playback without reading a buffer that is being filled. This recovery
can cause an audible dropout; hardware performance remains to be measured.

The command uses 1x gain and active output (0x3000 | sample), mapping the
signed audio range to 12-bit unsigned codes. The MCP4821 uses its internal
2.048 V reference; SHDN must be high for playback.

Run `python3 tests/test_dac_pio.py` to check sample frame length, bit ordering,
clock edges, and LDAC sequencing. Firmware build and waveform simulation do
not replace a logic-analyzer check on the actual board.


## ADC acquisition

`input_init()` initializes the three Pico ADC inputs on GP26–28 and a PIO1
state machine for the MCP3208: CS GP7, CLK GP8, DIN GP9, DOUT GP10.
`poll_controls()` runs on core 0 in the main loop alongside MIDI polling.
It starts a new scan no more often than every 1 ms, then collects conversions
without waiting for either converter. The MCP3208 SPI clock is 500 kHz;
PIO completes each 24-clock transaction independently of CPU polling.
The Pico conversions use the internal ADC hardware in single-shot mode.
There is no ADC DMA or ADC interrupt handler.

A scan takes roughly 0.4 ms plus UI polling overhead. If the main loop slows
down, scanning slows down rather than accumulating a backlog. The ADC driver emits no parameter messages itself; the update hook routes
assigned external channels through the messenger. Channels have no fixed
voice assignments.

`input_get_snapshot(&snapshot)` returns false until the first valid scan.
Thereafter it copies the most recent complete scan under a short lock:

```c
InputSnapshot snapshot;
if (input_get_snapshot(&snapshot)) {
    // snapshot.pico[0..2]: ADC0..2, GP26..28
    // snapshot.external[0..7]: MCP3208 CH0..7
    // snapshot.pico_normalized[] / external_normalized[]: calibrated 0..1
    // snapshot.sequence: increments for each published scan
    // snapshot.timestamp_us: scan completion time
}
```

The `pico[]` and `external[]` arrays remain raw unsigned 12-bit values, 0..4095.
`pico_normalized[]` and `external_normalized[]` provide calibrated 0..1 values
using each converter's shared calibration. Channels are sampled
sequentially, not simultaneously. A Pico conversion error discards the scan.
Snapshots can be read from either core after initialization, outside ISRs;
initialize once on core 0 before launching the audio core.

Run `python3 tests/test_mcp3208_pio.py` for command/response waveform tests
across all eight channels. Actual analog readings and scan rate still need
verification on the board.


### ADC update hook

`adc_updated(InputAdc adc, unsigned channel, uint16_t value)` in `tantricyphons.c`
is registered with `input_set_update_handler()`. Every valid full scan first
publishes the snapshot, then calls the hook once for each of the 11 channels.
`INPUT_ADC_PICO` identifies Pico ADC0–2; `INPUT_ADC_MCP3208` identifies external
CH0–7. Together, converter and channel form the key for future menu assignments.

Notifications mean a fresh reading is available, not necessarily that its
value changed. There is no threshold or smoothing yet. They run on core 0
outside interrupt context and outside the snapshot lock. Keep the handler
short and nonblocking so MIDI and ADC polling remain responsive. The registered handler forwards MCP3208 samples to the assignment router;
channels assigned OFF have no effect. Register NULL to disable notifications. The Pico's built-in ADC uses its own conversion
hardware; PIO handles the external MCP3208 serial transactions.

## LCD and monk playback

The ST7789 portrait display uses SPI0: CS GP1, SCK GP2, MOSI GP3, D/C GP4,
reset GP5, and backlight GP6. The baseline assumes 240x320, zero panel offsets,
RGB color order and inversion enabled, following the earlier display prototype.
`DISPLAY_SPI_HZ` in `display.h` starts at 12.5 MHz for hardware bring-up.
This is below the 15.15 MHz limit implied by the ST7789V's 66 ns write-cycle
specification. Faster speeds need testing with the actual panel and wiring:
https://dl.espressif.com/dl/schematics/ST7789V_SPEC_V1.0.pdf

The original 30 packed 4bpp frames and palette use 201,632 flash bytes.
CMake generates a C source file from `assets/` at build time. One reusable
107,520-byte RAM buffer holds the selected frame expanded 2x to 240x224 RGB565.
The monk occupies the top 224 rows; the lower 96 rows remain black for future
menu UI. The initial full-screen clear is also DMA-driven using one repeated
black byte rather than a second framebuffer.

`display_poll()` runs on core 0 alongside MIDI and ADC polling. Panel reset
and startup delays use deadlines rather than sleeps. Once initialized, each
poll expands at most one 120-pixel source row, including 2x horizontal/vertical
scaling. SPI TX DREQ then paces DMA for the complete pixel image. Polling waits
for both DMA completion and an idle SPI shifter before releasing CS or reusing
the image buffer. There is no display DMA interrupt and no PIO allocation.
Dummy RX data and overrun status are drained after transmission.

At 12.5 MHz, pixel transmission takes approximately 69 ms per monk frame,
plus incremental decoding and command overhead. Unchanged frames are skipped;
rapid vowel changes are coalesced to the current state when selecting the next
frame. Frame starts are capped at 30 Hz, but SPI bandwidth at the initial speed
limits the actual rate to less than 15 Hz. No tearing synchronization is used.

The audio core publishes a tiny `SoundState` (current vowel and active/release
state) after each audio buffer under a short lock. The display reads that
snapshot, leaving the voice owned by core 1. Vowel selects frames 6–29 while
sounding; silence follows the VST's hold/blink/shuffle animation. External ADC channels start unassigned. Once assigned through the menu,
the monk follows any resulting changes to the voice vowel.

The firmware currently occupies about 245 KiB flash and 276 KiB static RAM,
including the voice and display buffers. These are build sizes, not runtime
performance measurements. Orientation, colors, actual scan/refresh rates and
combined audio timing still require hardware verification.

Host animation/decoder checks (including all frames and output bounds):

```sh
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -Iinclude \
    src/display/monk.c build/monk_assets.c tests/test_monk.c -o /tmp/tantricyphons-test-monk
/tmp/tantricyphons-test-monk
```


## Input assignment menu

The lower 240x96 LCD area displays four inputs per page, leaving the monk above.
MCP3208 CH0–3 map to POT 1–4 and CH4–7 map to CV 1–4, matching the net labels
EX_POT_0..3 and EX_CV_0..3 on U2 in `hw/main/main.kicad_sch`. Pico ADC0–2 remain available through the raw update/snapshot APIs without menu
assignments; ADC0 additionally feeds the default 1 V/oct pitch path.

- Turn the GP16/17 encoder to select an input (scrolling to the second page).
- Press the encoder OK switch on GP18 to edit its destination.
- Turn to choose OFF, PITCH, VOWEL, VOICE, VIBRATO, VIB RATE, GLIDE, ASPIRATION,
  LEVEL, ATTACK, DECAY, SUSTAIN, or RELEASE.
- Press OK again to accept. The selected input takes control on its next scan.
- Press the separate Back switch on GP19 to cancel an uncommitted edit.
- Back while browsing closes the list; OK reopens it.

Assignments default to OFF. Saved assignments and both ADC calibrations load at
boot; use Setup > Save settings to persist edits. Cancel leaves the prior
assignment intact.
Turning an assignment OFF stops its future updates, leaving the parameter at
its last value. Assignments affect the voice through the existing core-0 to
core-1 event queue. Menu assignments do not generate note gates; GP20 provides the CV gate.

First-version control policy: the most recently changed assigned input wins,
including when several inputs target one parameter. No additive CV modulation
or soft takeover is implemented. A 16-code threshold relative to the last
sent reading suppresses small ADC fluctuations, and each source sends no more
than once per 20 ms. Failed enqueues are retried on subsequent scans.
This threshold is noise suppression, not voltage calibration or smoothing.

Raw 0..4095 maps to normalized 0..1 for most destinations. Assignable external PITCH spans MIDI
notes 36..84 (approximately 65.4..1046.5 Hz); ATTACK and DECAY span 0..2 seconds,
and RELEASE spans 0..4 seconds. CV uses the same raw ADC range for now; bipolar
scaling, offsets, modulation depth and per-input calibration are future work.
MIDI notes and controllers have priority as described below. Pico ADC0 and gate GP20 now provide a separate default CV playing path;
ADC1/2 now feed the dedicated vowel CV/pot combination.

Encoder quadrature edges are counted continuously by PIO2 on GP16/17.
Core 0 reads accumulated counts about once per millisecond and converts four
edges to a detent. No GPIO edge interrupts are used. OK and Back are polled
with 20 ms debounce, generating one action per press. GPIO16–19 use internal
pull-ups and assume contacts to ground. Verify direction and detent behavior
on the actual encoder.

Menu redraws use the same 105 KiB pixel buffer and SPI DMA channel as the monk.
A frozen view is rendered one row per poll, then sent to LCD rows 224..319.
It redraws only when the menu revision changes and is prioritized between monk
frames. No pixel buffer is overwritten while DMA or the SPI shifter is active.
The 8x8 font is reused from `st7789_mpy`; its MIT notice is in
`assets/font-LICENSE.txt`.

Host checks for edit/accept/cancel, page navigation, noise/rate limits,
queue retry, parameter scaling, and bounds-checked rendering:

```sh
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -Iinclude     src/display/menu.c src/controls/routing.c src/controls/calibration.c src/settings/settings_format.c src/settings/settings.c src/audio/voice.c \
    tests/settings_memory.c tests/test_menu.c -lm -o /tmp/tantricyphons-test-menu
/tmp/tantricyphons-test-menu
```


## Standalone CV playing and MIDI priority

The local KiCad control schematic is the source of truth for this wiring.
GP26/ADC0 reads FREQ_CV; GP20 reads the LM393 output named ~GATE on the control
sheet. The gate is active low, with a pull-up. The R18=20K / R10=30K divider
scales pitch-jack voltage by 0.6, followed by a unity buffer. With the 3.0 V
ADC reference, ADC full-scale nominally represents 5 V at the jack.

`cv_pitch.c` implements 1 V/oct with C2 (MIDI 36, 65.4 Hz) at 0 V,
so 1 V gives C3 and 5 V gives C7 (2093 Hz). `calibration.c` starts with nominal
ADC counts 819 at 1 V and 3276 at 4 V. The calibration menu replaces those
points with measured values; verify tracking on the actual board.

On core 0 the ADC hook caches fresh ADC0 pitch. `cv_input_poll()` sends pitch
changes and polls GP20 each main-loop pass. It waits for the first valid ADC
scan before enabling gate events. A gate status heartbeat every 10 ms allows
CV playing to recover if the event queue overflowed. Repeated gate-high status
does not retrigger the envelope. Gate is not button-debounced. Pulses shorter
than the main-loop service interval can be missed; verify with the intended
gate source. Pitch scan rate is nominally 1 kHz.

`voice_control.c` owns arbitration on core 1, with source-tagged events from
`add_message()` (ADC/panel) and `add_midi_message()` (MIDI):

- With no held MIDI notes, CV gate triggers/releases the voice and the latest
  panel pitch drives it. No menu assignment or MIDI connection is required.
- MIDI Note On takes pitch/gate priority. CV pitch and gate remain tracked,
  but cannot retune or stop a held MIDI note. MIDI retains last-note priority.
- Releasing the last MIDI note restores the latest CV pitch and gate together.
  Gate high resumes CV playing; gate low releases the voice normally. Returning
  to a high CV gate starts the voice through its normal note-on/envelope path.
- MIDI all-notes-off/reset releases MIDI ownership and restores CV state.
  Queue overflow releases both note sources; fresh CV status can resume CV.
- MIDI CC/wheel updates take ownership only of their respective parameters.
  New MIDI updates refresh a 2-second lease. ADC updates are cached while the
  parameter is owned. Unrelated parameters remain controllable from the panel.
- After the lease expires, a 100 ms ramp returns to the latest cached ADC value.
  Without an ADC assignment the last MIDI value remains. A new MIDI update
  interrupts any handback and immediately takes priority again.

Assignments among panel sources still use the most recently changed source.
An external channel assigned PITCH shares the cached panel pitch with ADC0;
the dedicated ADC0 path also sends changes. There is no CV summing, physical
MIDI cable detection, sustain-pedal handling, or active-sensing watchdog yet.
Disconnecting MIDI while a note is held cannot synthesize a missing Note Off.

Host ownership/scaling checks:

```sh
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -Iinclude     src/audio/voice_control.c src/controls/cv_pitch.c src/controls/calibration.c src/audio/voice.c tests/test_voice_control.c -lm     -o /tmp/tantricyphons-test-voice-control
/tmp/tantricyphons-test-voice-control
```


## Audio diagnostics through the debug probe

`TANTRICYPHONS_AUDIO_DEBUG` defaults ON. Core 0 sends one cumulative summary per second
on UART0 TX, GP0 (physical pin 1), at 115200 baud, 8N1. Connect target GP0 to
the probe UART RX signal and connect grounds. Leave probe TX disconnected:
GP1 is LCD CS and must not be connected as target UART RX. SWD uses its
separate SWCLK/SWDIO/GND connection. The target now also has a separate USB serial console (enabled by default);
the probe still presents its UART bridge to the host over USB.

`audio_debug_poll()` formats a short line once per second and feeds at most
eight bytes per UI-loop pass when the UART FIFO has space. UART writes never
wait for room. No logging occurs inside a DMA interrupt or on the audio core.
Counters and latest timing remain available to the source debugger as well.

Example (illustrative values, not a hardware measurement):

```text
audio blocks=172 A/B=85/85 late=0 underrun=0 fill(us) last/avg/max=3500/3450/3700 budget=5804 slow=0 dma=1
```

- `blocks`: total rendered/submitted buffers, including the initial pair.
- `A/B`: separate completed DMA block counts, normally alternating.
- `late`: next RAM buffer was not ready when DMA completed. The PIO FIFO may
  still cover that gap, so this does not necessarily mean an audible dropout.
- `underrun`: sticky PIO TXSTALL observations, sampled before each DMA restart.
  Indicates real FIFO starvation. Repeated checks do not count the same
  ongoing stall, but multiple stalls within one transfer can coalesce. This
  is not a count of missing samples or the duration of an interruption.
- `fill(us) last/avg/max`: time from acquiring a free buffer through message
  processing, synthesis, DAC packing and submit. Excludes waiting for a free
  buffer; includes any IRQ preemption during that interval. Average and max
  are cumulative since boot, including startup.
- `budget`: actual block duration from the configured PIO divider, rounded
  down to microseconds (about 5804 us at 150 MHz).
- `slow`: number of fills longer than that block budget, another diagnostic
  signal rather than an exact count of audible glitches.
- `dma`: currently active RAM buffer (0/1), or -1 if neither is transferring.

FIFO is primed before enabling PIO so normal startup does not register as an
underrun. The audio core publishes a coherent snapshot after each completed
fill. A hard hang on that core can leave the published counters frozen; a
breakpoint can itself cause underruns. These statistics need checking on the
actual board under combined MIDI/CV/display load.

Disable serial output with `cmake -S . -B build -DTANTRICYPHONS_AUDIO_DEBUG=OFF` and
rebuild. Counters remain collected and available through the snapshot API.
Official probe wiring reference:
https://www.raspberrypi.com/documentation/microcontrollers/debug-probe.html


## Calibration and persistent settings

Setup now has four entries: Input assignments, ADC calibration, Save
settings, and Reset defaults. OK opens/accepts; Back returns/cancels.
An asterisk in the heading marks unsaved settings. Assignment edits are live
in RAM; accepting an edit is distinct from writing settings to flash.

Choose **Pico ADC** or **MCP3208** inside ADC calibration. Each converter has
one shared calibration, applied to all its channels:

| Converter | Reference jack/channel | ADC voltage at jack 1 V / 4 V |
| --- | --- | --- |
| Pico ADC0–2 | FREQ CV / ADC0 | 0.6 V / 2.4 V |
| MCP3208 CH0–7 | CV 1 / CH4 | 1.8 V / 2.7 V |

The input conditioning differs: FREQ CV uses a 0.6 divider/unity buffer;
MCP CV1 uses a 0.3 gain and 1.5 V offset (20K, 30K and 12K summing network).
The wizard accounts for these circuits to fit **ADC-pin voltage**, rather
than treating jack voltages as identical across the two paths.

1. Select the converter and apply 1.000 V to its reference jack. Let it settle
   and press OK. The next 64 samples of that reference channel are averaged.
2. Apply 4.000 V and repeat. Keep each voltage steady throughout capture.
3. Review the ADC counts. OK applies the selected converter's calibration;
   Back cancels and leaves the previous calibration untouched.
4. Choose Save settings and confirm to retain both calibrations and assignments.

Captures reject more than 16 counts of spread, near-rail readings, reversed or
implausible spans and non-finite values. Only the selected converter/reference
channel contributes samples to its capture. This is a shared converter-level
correction; differences among individual channels' resistors are not measured.

Nominal defaults are Pico 819/3276 counts and MCP3208 2457/3685.5 counts at
jack 1 V/4 V. Corrected ADC voltage comes from linear interpolation/extrapolation
between these points; normalized values divide ADC-pin voltage by 3.0 V and
clamp to 0..1. Snapshot raw readings and the raw update callback stay unchanged.
The default bipolar MCP CV input therefore remains centered around 0.5 at
0 V jack input. This change does not turn it into a unipolar 1 V/oct input.
External assignments retain their existing parameter ranges, using corrected
normalized values. Calibration changes force unchanged assigned readings to
refresh; the dedicated Pico pitch path also refreshes when its bank changes.

Pico frequency voltage additionally divides ADC-pin voltage by 0.6, then uses
1 V/oct with C2 at 0 V. Corrected Pico ADC1/2 values feed the dedicated vowel CV/pot combination
and remain available in snapshots.

Reset defaults restores OFF assignments and nominal calibration for **both**
converters in RAM. A separate Save settings makes that reset persistent.
A failed save is displayed and can be retried.

Storage uses the final two 4 KiB flash sectors of the 4 MiB Pico 2: offsets
0x3FE000 and 0x3FF000. Each record is one 256-byte page with magic, schema
version, sequence number, eight stable assignment IDs, two float calibration
points per converter, and CRC32. Version 2 stores both converters. Legacy
version 1 records retain their Pico calibration/assignments and receive the
nominal MCP3208 calibration; the next changed save uses version 2. Saves erase/program the alternate sector, read it back,
and leave the previous valid sector intact. Boot selects the newest valid
record and falls back to defaults if neither validates. Unknown schemas,
invalid target IDs, invalid calibration and corrupt CRCs are rejected.
Unchanged saves avoid writing flash. Bump the schema when target IDs change.

A post-build binary-size check and a runtime image-end guard protect the
reserved region. Storage is intentionally tied to the Pico 2's 4 MiB layout;
a board/flash-size change requires updating both guards. Ordinary firmware
updates that do not touch these sectors can retain settings; a full-chip
erase clears them.

The audio core initializes the Pico SDK flash lockout mechanism. Save requests
wait for display startup and an idle display DMA channel, then execute the
flash operation with IRQs disabled and the other core locked out. The flash
callback and program data are in RAM. Audio DMA/PIO may drain and stall;
audio/control interruptions and underrun-counter increments during a save are
expected and accepted. Resume uses the existing buffer recovery path. Flash
writing and power-interruption recovery still require an on-board check;
host tests simulate records and partial programming, not physical flash.

Additional host tests:

```sh
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -Iinclude \
    src/controls/calibration.c src/settings/settings_format.c src/settings/settings.c src/controls/routing.c src/audio/voice.c \
    tests/settings_memory.c tests/test_settings.c -lm -o /tmp/tantricyphons-test-settings
/tmp/tantricyphons-test-settings
```


## Dedicated vowel pot and CV

GP28 / Pico ADC2 reads VOWEL_POT; GP27 / ADC1 reads VOWEL_CV. Both use the
shared Pico calibration. The main input hook caches their raw readings, then
`vowel_controls.c` combines them on core 0 after a complete scan.

The pot supplies a bipolar offset: full left = -1, center = 0, full right = +1.
The voice accepts a 0..1 vowel position, so this offset is scaled around 0.5:

```text
pot_offset = 2 * calibrated_pot_normalized - 1
cv_volts = calibrated_cv_adc_volts / 0.6
vowel = clamp(0.5 + 0.5 * pot_offset + 0.1 * max(cv_volts, 0), 0, 1)
```

With zero CV, left/center/right select vowel 0/0.5/1. At center, +1 V CV gives
0.6 and +5 V gives 1.0. The dedicated vowel CV path has a 20K/30K divider and
unity buffer, without the bipolar offset on the assignable MCP CV inputs.
It therefore measures unipolar 0..5 V; negative jack voltage is clipped by the
input/ADC protection and cannot produce negative modulation readings.
`VOWEL_CV_DEPTH` and `VOWEL_CV_FULL_SCALE_VOLTS` in `vowel_controls.h` specify
the initial depth (0.5 voice-vowel units per 5 V), ready for listening tests.

No output is sent before both inputs have real readings. Small combined-value
changes below 4/4095 are suppressed, with at most one update per 5 ms.
Queue failures retry; Pico calibration changes force a refresh even when the
raw readings are unchanged. The combined value goes through the same MIDI
priority/handback logic as other ADC parameters. External inputs assigned to
vowel retain the existing most-recently-changed-source behavior. The monk
follows the resulting voice vowel through the existing audio state snapshot.

Checks cover pot endpoints/center, CV addition and clamping, calibration,
startup validity, noise/rate limits, queue retry, and MIDI handback:

```sh
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -Iinclude \
    src/controls/vowel_controls.c src/controls/calibration.c src/audio/voice_control.c src/audio/voice.c \
    tests/test_vowel_controls.c -lm -o /tmp/tantricyphons-test-vowel
/tmp/tantricyphons-test-vowel
```


## PIO rotary encoder

`encoder.pio` occupies 27 instructions at address 0 in PIO2, using one state
machine at 1 MHz. PIO0 remains the DAC and PIO1 the MCP3208. GP16 is the low
input bit (A), GP17 the high bit (B). A 16-entry previous/current-state jump
table counts valid quadrature transitions in a wrapping 32-bit Y register;
unchanged/illegal transitions do nothing. Reverse bounce reverses its count.
The longest decoder path takes ten cycles, suitable for roughly 100k valid
edges per second; real hardware debounce/sampling behavior remains to verify.

PIO pushes cumulative counts without blocking into a joined eight-word RX
FIFO. Core 0 drains at most eight samples per 1 ms poll and never waits for a
new sample. FIFO-full snapshots can be stale, but Y keeps counting independently,
so later polls recover the net movement. This adds at most roughly two polls
of normal delivery latency. During flash saves PIO continues counting while
the CPUs pause; any motion is delivered after the UI resumes.

`encoder_steps.c` converts modular edge-count differences into detents,
preserving partial steps and handling both directions through counter wrap.
`ENCODER_EDGES_PER_DETENT` defaults to four. Decoder sign matches the prior
GPIO handler for the same A/B pin transitions. The encoder push and Back
buttons retain GPIO polling/debounce. No encoder DMA or IRQ is needed.

Reference approach:
https://github.com/raspberrypi/pico-examples/tree/master/pio/quadrature_encoder

```sh
python3 tests/test_encoder_pio.py
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -Iinclude \
    src/controls/encoder_steps.c tests/test_encoder_steps.c -o /tmp/tantricyphons-test-encoder
/tmp/tantricyphons-test-encoder
```

The simulation checks startup with all four pin states, both directions,
contact bounce, ignored invalid transitions, FIFO-full operation, and counter
wrap. Physical direction and steps per detent still need board verification.


## USB serial console

`TANTRICYPHONS_USB_CONSOLE` defaults ON, enabling SDK USB CDC stdio and the standard
USB reset interfaces. Flash this firmware once using BOOTSEL, then open the
new USB serial port in a serial terminal (115200 is conventional; USB CDC
transport is not paced by that UART baud setting). A debug probe is not
required. Disable local echo because the console echoes typed input itself.
The greeting is `tantricyphons USB console. Type help.` and the prompt is `> `.

```text
help
info
status
adc
note 69
set vowel 0.7
set vibrato 0.2
off
routes
assign 0 vibrato
cal
save
```

- `info`: board, clock, nominal sample rate, buffer size, pin/peripheral summary,
  unsaved settings and save-state (`idle`, `pending`, `saved`, `error`).
- `status`: voice note/vowel state, audio A/B completions, late buffers,
  underrun observations, and fill-time diagnostics.
- `adc`: one snapshot of all three Pico and eight MCP3208 channels, showing
  raw values, calibrated normalized values, scan number and age.
- `note NOTE [VELOCITY]`: enqueue a MIDI-style note (0..127), default velocity
  100. Velocity zero is Note Off. The existing voice ignores velocity amplitude.
- `off [NOTE]`: release one note or all MIDI notes. CV gate/pitch remains
  available and can resume playing; this does not forcibly mute a high CV gate.
- `set PARAM VALUE`: enqueue a MIDI-priority parameter message. Parameters
  `vowel`, `voice`, `vibrato`, `vib_rate`, `glide`, `aspiration`, `level`, and
  `sustain` take 0..1; `attack`/`decay` take 0..2 seconds and `release` 0..4.
  The normal 2-second MIDI ownership lease and ADC handback apply. Use `note`
  for pitch testing rather than a direct pitch command fighting the CV path.
- `routes`: show all eight external input destinations.
- `assign INPUT TARGET`: change a RAM assignment. Inputs 0..3 are pots 1..4;
  4..7 are CV 1..4. Targets are `off`, `pitch`, or any `set` parameter.
  The LCD menu refreshes after these programmatic changes.
- `cal`: show both ADC calibration records without changing them.
- `save`: request the existing safe flash save, including calibration and
  assignments; audio may pause. Check `info` for completion. There is no
  automatic save after an assignment edit.

The console runs only on core 0. Each poll handles at most 16 input bytes and
48 output bytes. A 4 KiB output ring and USB TX-space checks retain replies
under normal host backpressure; input processing pauses before output runs
out of room. SDK USB stdout/mutex timeouts are zero to avoid waiting for an
unresponsive/disconnected terminal. Only this thread writes USB; periodic
probe diagnostics remain on the independent UART0 path. A disconnect resets
partially typed input and pending replies, without automatically releasing
notes. A line is limited to 95 characters; overflow is discarded through the
next newline. CR, LF, CRLF, backspace/delete and Ctrl-C line cancel are handled.
Commands are lower-case and whitespace-separated. There is no terminal history.

Numeric input rejects non-finite values, malformed tokens and out-of-range
values. Event enqueue failures are reported. No command accesses arbitrary
memory, touches the voice from core 0, or runs terminal I/O in an audio IRQ.
The parser is hardware-independent and tested with malformed/bounded random
input under ASan/UBSan. Live USB enumeration, terminal interaction and combined
audio timing still need checking on the physical Pico.

The SDK's USB reset support should also allow picotool/Run Project to reboot
this running firmware into BOOTSEL for later uploads. Source-level breakpoints
still require the SWD probe. Configure `-DTANTRICYPHONS_USB_CONSOLE=OFF` to return to a
USB-disabled build; the console entry points then become no-ops.

```sh
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -Iinclude \
    src/diagnostics/console_command.c tests/test_console_command.c -lm -o /tmp/tantricyphons-test-console
/tmp/tantricyphons-test-console
```

### Connectivity observations

`connections` distinguishes firmware activity from verified physical presence.
USB host serial connection is known active while the command runs. DAC DMA
completions and PIO stalls describe the transmitter, not whether an MCP4821
is installed: its write-only connection provides no acknowledgement. The LCD
also has TX-only wiring, so its presence is unverified.

MCP3208 scans similarly have no device ID or ACK. The command reports raw
near-rail channel counts (0..4 or 4091..4095), an all-zero hint, scan number
and age. A missing/unpowered chip, wiring issue, or legitimately grounded
inputs can produce indistinguishable readings. An absent converter may still
produce apparently varying readings on a floating pin; that is not proof of
connectivity. Internal Pico ADC availability does not establish connections
to its analog inputs.

MIDI UART received-byte/error counters and last-traffic age show activity,
without claiming a source is connected or its bytes are valid MIDI. GPIO
levels and the encoder edge count can help test switches/knob movement; idle
and unconnected contacts with pull-ups look alike. These are non-invasive
observations, not automatic cable or chip detection. Use a scope or a planned
loopback measurement to confirm actual DAC output.

## Rendering headroom and sample-rate builds

A held note still triggers grains periodically. The renderer previously
recomputed two power functions every active sample and rebuilt the entire
waveform at every grain trigger, including when its inputs were identical.
`voice.c` now caches pitch-to-period results by quantized pitch and sample rate,
vibrato rate scaling by its input parameter, and grain waveforms by vowel,
voice character, aspiration, sample rate, length and dirty state. Grain overlap
still happens at every original trigger. Its ring traversal now uses a wrapping
index rather than a modulo operation per sample.

This does not approximate or change the cached calculations. The host
before/after differential test rendered 356,580 samples over seven sample
rates, stable notes, parameter changes, envelopes, reset and sample-rate
changes; outputs were byte-for-byte identical. Separate tests check cache reuse
and invalidation. Target performance and target waveform equivalence still
need confirmation on the board.

`status` now reports message handling, render, and pack/submit times separately,
plus grains triggered and waveforms built in the most recent and worst-fill
blocks. Independent per-stage maxima need not come from the same block;
`worst fill phases` and its grain/build counts do. In a steady voice, most
triggered grains should require no waveform rebuild. Floating unconnected
vowel ADC inputs can still change vowel, causing legitimate rebuilds.

The sample rate is configurable with `TANTRICYPHONS_SAMPLE_RATE` (integer 8000..48000,
default 44100; suggested build choices 22050, 32000, 44100). For 256 samples,
nominal deadlines are 11.61 ms at 22050, 8 ms at 32000 and 5.80 ms at 44100.
Clock-divider rounding slightly changes the actual rate, reported as the block
budget in diagnostics. No system-clock overclocking is used.

The current trial artifacts are:

- `build/tantricyphons.uf2` and `build/tantricyphons.elf`: optimized 44.1 kHz firmware.
- `build/22050/tantricyphons.uf2` and `build/22050/tantricyphons.elf`: optimized 22.05 kHz firmware.

The nested build is independent and ignored with the rest of `build/`. Match
the ELF to the firmware when using the source debugger. Use `info` to confirm
the nominal rate after flashing. Both retain the USB console and saved-settings
format. The heartbeat slows proportionally at the lower sample rate.

To change the primary build rate:

```sh
cmake -S . -B build -DTANTRICYPHONS_SAMPLE_RATE=22050
cmake --build build
```

Cache checks:

```sh
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -Iinclude \\
    src/audio/voice.c tests/test_voice_cache.c -lm -o /tmp/tantricyphons-test-voice-cache
/tmp/tantricyphons-test-voice-cache
```

`tests/render_voice_samples.c` is the waveform fixture used to compare the
original and optimized implementations. Compile it separately against each
voice.c/header version, write sample files, and compare them with `cmp`.

### Resetting an audio measurement window

Use `resetstats` after starting a test note, then `status` after letting it run.
Core 0 requests the reset; core 1 services it at the next acquired audio buffer.
The request is protected by the existing state lock, and DAC counters are
cleared with core-1 IRQs briefly disabled. Playback, the voice/envelope, caches,
MIDI/CV ownership, DMA/PIO configuration and buffer ownership are preserved.

The command resets rendered-block totals, fill average/max, slow-block count,
per-stage timings/worst-block context and DAC start/completion/late/underrun
counters. The block budget stays intact. PIO's sticky historical stall flag
is cleared. In-flight buffers continue, so A/B completions in a fresh window
can include a transfer that started before the reset. MIDI traffic counters
and ADC scan sequence are independent and are not reset.

`status` reports `stats window=N`, incremented when core 1 performs a reset.
A command immediately following the request can still see the previous window
until the next audio buffer completes (normally about 5.8 ms at 44.1 kHz).
Repeated requests before that boundary coalesce. The configured primary
sample rate remains 44.1 kHz.

```text
note 69
resetstats
status
```
