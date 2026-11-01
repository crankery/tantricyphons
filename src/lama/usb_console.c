#include "usb_console.h"
#if LAMA_USB_CONSOLE
#include "console_command.h"
#include "soundCore.h"
#include "input.h"
#include "calibration.h"
#include "settings.h"
#include "midi.h"
#include "ui_controls.h"
#include "cv_input.h"
#include "hardware/clocks.h"
#include "pico/stdio_usb.h"
#include "tusb.h"
#include <inttypes.h>
#include <stdarg.h>
#include <string.h>

#define OUTPUT_SIZE 4096
#define REPLY_SIZE 1536
static char output[OUTPUT_SIZE];
static unsigned output_read, output_write;
static char line[CONSOLE_LINE_SIZE];
static unsigned line_length;
static bool overflow, connected, previous_cr;
static char reply[REPLY_SIZE];
static unsigned reply_length;

static unsigned available(void)
{
    return OUTPUT_SIZE - 1 - (output_write + OUTPUT_SIZE - output_read) % OUTPUT_SIZE;
}

static void emit(const char *text)
{
    while (*text && available())
    {
        output[output_write] = *text++;
        output_write = (output_write + 1) % OUTPUT_SIZE;
    }
}

static void append(const char *format, ...)
{
    if (reply_length >= sizeof(reply) - 1)
    {
        return;
    }

    va_list args;
    va_start(args, format);
    int size = vsnprintf(reply + reply_length, sizeof(reply) - reply_length, format, args);
    va_end(args);
    if (size > 0)
    {
        unsigned room = sizeof(reply) - reply_length - 1;
        reply_length += (unsigned)size > room ? room : (unsigned)size;
    }
}

static const char *save_state(void)
{
    switch (settings_status())
    {
    case SETTINGS_IDLE:
        return "idle";
    case SETTINGS_PENDING:
        return "pending";
    case SETTINGS_SAVED:
        return "saved";
    case SETTINGS_ERROR:
        return "error";
    }

    return "unknown";
}

static void execute(void)
{
    ConsoleCommand command;
    const char *error;
    reply_length = 0;
    reply[0] = '\0';
    if (!console_parse(line, &command, &error))
    {
        append("Error: %s\r\n", error);
        emit(reply);
        return;
    }

    switch (command.action)
    {
    case CONSOLE_HELP:
        append("help | info | status | resetstats | connections | adc | routes | cal | save\r\n"
               "note NOTE [VELOCITY]  (0..127; default velocity 100)\r\n"
               "off [NOTE]           (release MIDI notes; CV gate remains available)\r\n"
               "set PARAM VALUE      (0..1, except attack/decay 0..2s, release 0..4s)\r\n"
               "  vowel voice vibrato vib_rate glide aspiration level\r\n"
               "  attack decay sustain release\r\n"
               "assign INPUT TARGET  (0..3 pots, 4..7 CV; off/pitch/PARAM)\r\n"
               "set uses MIDI priority: 2s hold, then 100ms ADC handback.\r\n"
               "assign edits RAM; save persists assignments/calibration (audio may pause).\r\n");
        break;
    case CONSOLE_INFO:
        append("lama / Pico 2 / RP2350\r\n"
               "clock=%" PRIu32 " Hz, audio=%u Hz nominal, buffer=%u samples\r\n"
               "DAC PIO0 / ADC PIO1 / encoder PIO2\r\n"
               "MIDI UART1 GP21 / probe TX UART0 GP0\r\n"
               "settings=%s, save-state=%s\r\n",
               clock_get_hz(clk_sys),
               SAMPLE_RATE,
               SAMPLE_BUFFER_SIZE,
               settings_dirty() ? "unsaved changes" : "unchanged",
               save_state());
        break;
    case CONSOLE_STATUS:
    {
        AudioDebugStats stats = sound_core_get_debug_stats();
        SoundState sound = sound_core_get_state();
        append("voice active=%u vowel=%.4f\r\n"
               "blocks=%" PRIu32 " A/B=%" PRIu32 "/%" PRIu32 " late=%" PRIu32 " underrun=%" PRIu32
               " dma=%d\r\n"
               "fill(us) last/avg/max=%" PRIu32 "/%" PRIu32 "/%" PRIu32 " budget=%" PRIu32
               " slow=%" PRIu32 "\r\n",
               (unsigned)sound.note_active,
               (double)sound.vowel,
               stats.blocks,
               stats.dac.completions[0],
               stats.dac.completions[1],
               stats.dac.late_buffers,
               stats.dac.underruns,
               stats.dac.active_buffer,
               stats.last_fill_us,
               stats.blocks ? (uint32_t)(stats.total_fill_us / stats.blocks) : 0,
               stats.max_fill_us,
               stats.dac.block_budget_us,
               stats.over_budget);
        append("stats window=%" PRIu32 "\r\n", stats.reset_generation);
        append("last phases(us) messages/render/pack=%" PRIu32 "/%" PRIu32 "/%" PRIu32
               " grains/builds=%" PRIu32 "/%" PRIu32 "\r\n"
               "phase max(us) messages/render/pack=%" PRIu32 "/%" PRIu32 "/%" PRIu32 "\r\n"
               "worst fill phases(us)=%" PRIu32 "/%" PRIu32 "/%" PRIu32 " grains/builds=%" PRIu32
               "/%" PRIu32 "\r\n",
               stats.last_messages_us,
               stats.last_render_us,
               stats.last_pack_us,
               stats.last_grains,
               stats.last_grain_builds,
               stats.max_messages_us,
               stats.max_render_us,
               stats.max_pack_us,
               stats.worst_messages_us,
               stats.worst_render_us,
               stats.worst_pack_us,
               stats.worst_grains,
               stats.worst_grain_builds);

        break;
    }

    case CONSOLE_RESET_STATS:
        sound_core_reset_debug_stats();
        append("Stats reset requested for the next audio buffer; playback unchanged.\r\n");
        break;

    case CONSOLE_CONNECTIONS:
    {
        AudioDebugStats audio = sound_core_get_debug_stats();
        MidiDebugStats midi = midi_get_debug_stats();
        append("USB: host serial connection active\r\n"
               "DAC: DMA completed=%" PRIu32 ", stalls=%" PRIu32
               "; MCP4821 presence UNVERIFIED (write-only)\r\n",
               audio.dac.completions[0] + audio.dac.completions[1],
               audio.dac.underruns);
        InputSnapshot sample;
        if (input_get_snapshot(&sample))
        {
            unsigned rails = 0;
            bool all_zero = true;
            for (unsigned i = 0; i < INPUT_EXTERNAL_CHANNELS; ++i)
            {
                uint16_t value = sample.external[i];
                if (value <= 4 || value >= 4091)
                {
                    ++rails;
                }

                if (value != 0)
                {
                    all_zero = false;
                }
            }

            append("Pico ADC: internal peripheral; scan=%" PRIu32 " age=%" PRIu64 "us\r\n"
                   "MCP3208: presence UNVERIFIED (no ID/ACK); %u/8 channels near rails%s\r\n",
                   sample.sequence,
                   time_us_64() - sample.timestamp_us,
                   rails,
                   all_zero ? "; all zero" : "");
            if (rails == INPUT_EXTERNAL_CHANNELS)
            {
                append("  Check power/reference/wiring or input voltages; disconnected and "
                       "grounded can look alike.\r\n");
            }
        }
        else
        {
            append("ADC: no complete scan yet; use adc for individual readings\r\n");
        }

        append("LCD: presence UNVERIFIED (TX-only wiring)\r\n"
               "MIDI RX: bytes=%" PRIu32 " errors=%" PRIu32,
               midi.received_bytes,
               midi.receive_errors);
        if (midi.received_bytes)
        {
            append(" last=%" PRIu64 "ms ago", (time_us_64() - midi.last_rx_us) / 1000);
        }

        append("; traffic is not a presence handshake\r\n"
               "GPIO levels: encoder A=%u B=%u OK=%u Back=%u Gate=%u\r\n"
               "Encoder edges=%" PRIu32
               "; idle/open contacts look alike (buttons/gate active low).\r\n"
               "Running firmware peripherals do not prove external chips are connected.\r\n",
               (unsigned)gpio_get(ENCODER_A),
               (unsigned)gpio_get(ENCODER_B),
               (unsigned)gpio_get(ENCODER_SW),
               (unsigned)gpio_get(BACK_SW),
               (unsigned)gpio_get(GATE_PIN),
               ui_controls_encoder_count());
        break;
    }

    case CONSOLE_ADC:
    {
        InputSnapshot sample;
        if (!input_get_snapshot(&sample))
        {
            append("No valid ADC scan yet\r\n");
            break;
        }

        append("scan=%" PRIu32 " age=%" PRIu64 "us (raw / calibrated 0..1)\r\n",
               sample.sequence,
               time_us_64() - sample.timestamp_us);
        for (unsigned i = 0; i < INPUT_PICO_CHANNELS; ++i)
        {
            append("Pico %u GP%u: %u / %.4f\r\n",
                   i,
                   26 + i,
                   sample.pico[i],
                   (double)sample.pico_normalized[i]);
        }

        for (unsigned i = 0; i < INPUT_EXTERNAL_CHANNELS; ++i)
        {
            append("MCP %u %-5s: %u / %.4f\r\n",
                   i,
                   routing_input_name(i),
                   sample.external[i],
                   (double)sample.external_normalized[i]);
        }

        break;
    }

    case CONSOLE_ROUTES:
        for (unsigned i = 0; i < ROUTING_INPUTS; ++i)
        {
            append("%u %-5s -> %s\r\n",
                   i,
                   routing_input_name(i),
                   routing_target_name(routing_get_target(i)));
        }

        break;
    case CONSOLE_CAL:
        for (unsigned bank = 0; bank < CAL_BANKS; ++bank)
        {
            AdcCalibration value = calibration_get((CalibrationBank)bank);
            append("%s: raw at jack 1V=%.2f 4V=%.2f\r\n",
                   bank == CAL_PICO ? "Pico (FREQ CV)" : "MCP3208 (CV1)",
                   (double)value.raw_1v,
                   (double)value.raw_4v);
        }

        break;
    case CONSOLE_EVENT:
        append(add_midi_message(command.event, command.value) ? "Queued\r\n"
                                                              : "Queue full; retry\r\n");
        break;
    case CONSOLE_ASSIGN:
        routing_set_target(command.input, command.target);
        append("%s -> %s (RAM; use save to persist)\r\n",
               routing_input_name(command.input),
               routing_target_name(command.target));
        break;
    case CONSOLE_SAVE:
        settings_request_save();
        append("Save requested; audio may pause. Use info to check save-state (2=saved, "
               "3=error).\r\n");
        break;
    case CONSOLE_NONE:
        break;
    }

    emit(reply);
}

void usb_console_init(void)
{
    output_read = output_write = line_length = 0;
    overflow = connected = previous_cr = false;
}

void usb_console_poll(void)
{
    bool present = stdio_usb_connected();
    if (!present)
    {
        if (connected)
        {
            usb_console_init();
        }

        return;
    }

    if (!connected)
    {
        connected = true;
        emit("\r\nlama USB console. Type help.\r\n> ");
    }

    // Inspect TX room before consuming our ring. Only this core/thread writes
    // USB output; the background USB worker can free room but cannot fill it.
    // A slow reader therefore backs up the ring rather than losing replies.
    // Zero SDK timeouts still protect against a disconnect during the write.
    if (output_read != output_write)
    {
        char chunk[48];
        unsigned size = 0;
        uint32_t room = tud_cdc_write_available();
        while (size < sizeof(chunk) && size < room && output_read != output_write)
        {
            chunk[size++] = output[output_read];
            output_read = (output_read + 1) % OUTPUT_SIZE;
        }

        if (size)
        {
            stdio_put_string(chunk, (int)size, false, false);
        }
    }

    for (unsigned i = 0; i < 16 && available() >= REPLY_SIZE + CONSOLE_LINE_SIZE; ++i)
    {
        int value = getchar_timeout_us(0);
        if (value < 0)
        {
            break;
        }

        unsigned char c = (unsigned char)value;
        if (c == '\t')
        {
            c = ' ';
        }

        if (c == '\n' && previous_cr)
        {
            previous_cr = false;
            continue;
        }

        previous_cr = c == '\r';
        if (c == '\r' || c == '\n')
        {
            emit("\r\n");
            line[line_length] = '\0';
            if (overflow)
            {
                emit("Error: line too long\r\n");
            }
            else
            {
                execute();
            }

            line_length = 0;
            overflow = false;
            emit("> ");
        }
        else if (c == 3)
        {
            line_length = 0;
            overflow = false;
            emit("^C\r\n> ");
        }
        else if ((c == 8 || c == 127) && !overflow && line_length)
        {
            --line_length;
            emit("\b \b");
        }
        else if (!overflow && c >= 32 && c < 127)
        {
            if (line_length + 1 < sizeof(line))
            {
                line[line_length++] = (char)c;
                char echo[] = {(char)c, '\0'};
                emit(echo);
            }
            else
            {
                overflow = true;
            }
        }
    }
}
#else
void usb_console_init(void)
{
}

void usb_console_poll(void)
{
}
#endif
