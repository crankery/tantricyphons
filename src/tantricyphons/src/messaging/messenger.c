#include "messaging/messenger.h"
#include "audio/voice_control.h"
#include "pico/stdlib.h"
#include "pico/util/queue.h"
#include "pico/sync.h"

static queue_t monk_events;
static critical_section_t event_lock;
static bool overflow;
static VoiceControl control;

void start_messenger(void)
{
    voice_control_init(&control);
    queue_init(&monk_events, sizeof(MonkEvent), 32);
    critical_section_init(&event_lock);
}

static bool enqueue(MonkEventType e, float v, MonkEventSource source)
{
    assert(get_core_num() == 0);
    MonkEvent event = {.type = e, .value = v, .source = source};
    critical_section_enter_blocking(&event_lock);
    bool ok = !overflow && queue_try_add(&monk_events, &event);
    if (!ok)
    {
        overflow = true;
    }

    critical_section_exit(&event_lock);
    return ok;
}

bool add_message(MonkEventType e, float v)
{
    return enqueue(e, v, MONK_SOURCE_ADC);
}

bool add_midi_message(MonkEventType e, float v)
{
    return enqueue(e, v, MONK_SOURCE_MIDI);
}

void process_messages(MonkVoice *voice, float *master_level)
{
    assert(get_core_num() == 1);
    MonkEvent event;
    while (true)
    {
        critical_section_enter_blocking(&event_lock);
        if (overflow)
        {
            /* A lost note-off must not leave the voice stuck. Discard the
             * incomplete stream before accepting fresh events. */
            while (queue_try_remove(&monk_events, &event))
            {
            }

            overflow = false;
            voice_control_panic(&control, voice);
        }

        bool available = queue_try_remove(&monk_events, &event);
        critical_section_exit(&event_lock);
        if (!available)
        {
            break;
        }

        voice_control_event(&control, voice, master_level, event, time_us_64());
    }

    voice_control_tick(&control, voice, master_level, time_us_64());
}
