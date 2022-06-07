#include <stdbool.h>
#include <avr/sleep.h>
#include <avr/wdt.h>
#include <avr/interrupt.h>
#include "suspend.h"
#include "action.h"
#include "timer.h"

#ifdef PROTOCOL_LUFA
#    include "lufa.h"
#endif
#ifdef PROTOCOL_VUSB
#    include "vusb.h"
#endif
#ifdef SPLIT_KEYBOARD
#    include "split_common/split_util.h"
#endif

// TODO: This needs some cleanup

#if !defined(NO_SUSPEND_POWER_DOWN) && defined(WDT_vect)

// clang-format off
#define wdt_intr_enable(value) \
__asm__ __volatile__ ( \
    "in __tmp_reg__,__SREG__" "\n\t" \
    "cli" "\n\t" \
    "wdr" "\n\t" \
    "sts %0,%1" "\n\t" \
    "out __SREG__,__tmp_reg__" "\n\t" \
    "sts %0,%2" "\n\t" \
    : /* no outputs */ \
    : "M" (_SFR_MEM_ADDR(_WD_CONTROL_REG)), \
    "r" (_BV(_WD_CHANGE_BIT) | _BV(WDE)), \
    "r" ((uint8_t) ((value & 0x08 ? _WD_PS3_MASK : 0x00) | _BV(WDIE) | (value & 0x07))) \
    : "r0" \
)
// clang-format on

/** \brief Power down MCU with watchdog timer
 *
 * wdto: watchdog timer timeout defined in <avr/wdt.h>
 *          WDTO_15MS
 *          WDTO_30MS
 *          WDTO_60MS
 *          WDTO_120MS
 *          WDTO_250MS
 *          WDTO_500MS
 *          WDTO_1S
 *          WDTO_2S
 *          WDTO_4S
 *          WDTO_8S
 */
static uint8_t wdt_timeout = 0;

/** \brief Power down
 *
 * FIXME: needs doc
 */
static void power_down(uint8_t wdto) {
    wdt_timeout = wdto;

    // Watchdog Interrupt Mode
    wdt_intr_enable(wdto);

    // TODO: more power saving
    // See PicoPower application note
    // - I/O port input with pullup
    // - prescale clock
    // - BOD disable
    // - Power Reduction Register PRR
    set_sleep_mode(SLEEP_MODE_PWR_DOWN);
    sleep_enable();
    sei();
    sleep_cpu();
    sleep_disable();

    // Disable watchdog after sleep
    wdt_disable();
}

#if defined(SPLIT_KEYBOARD)
 /* 
    Matrix scan sychnronizes slave with master for split keyboards.
    Scan is invoked by keyboard_task() or suspend_wakeup_condition().
      - Keyboard task is not invoked when suspended. 
      - Wake up check is not invoked when remote wake is not enabled.
    This function forces master to slave sync without invoking the matrix_scan().
    Without the sync, slave would not receive data written by suspend_power_down().
    E.g. rgb lights would't turn off on slave half.
*/
static void suspend_sync_slave(void) {
    matrix_row_t master_matrix[MATRIX_ROWS / 2] = {0};
    matrix_row_t slave_matrix[MATRIX_ROWS / 2] = {0};
    transport_master_if_connected(master_matrix, slave_matrix);
}
#endif

/* watchdog timeout */
ISR(WDT_vect) {
    // compensate timer for sleep
    switch (wdt_timeout) {
        case WDTO_15MS:
            timer_count += 15 + 2; // WDTO_15MS + 2(from observation)
            break;
        default:;
    }
}

#endif

/** \brief Suspend power down
 *
 * FIXME: needs doc
 */
void suspend_power_down(void) {
#ifdef PROTOCOL_LUFA
    if (USB_DeviceState == DEVICE_STATE_Configured) return;
#endif
#ifdef PROTOCOL_VUSB
    if (!vusb_suspended) return;
#endif

    suspend_power_down_quantum();
#ifdef SPLIT_KEYBOARD
    suspend_sync_slave();
#endif

#ifndef NO_SUSPEND_POWER_DOWN
    // Enter sleep state if possible (ie, the MCU has a watchdog timeout interrupt)
#    if defined(WDT_vect)
    power_down(WDTO_15MS);
#    endif
#endif
}

/** \brief run immediately after wakeup
 *
 * FIXME: needs doc
 */
void suspend_wakeup_init(void) {
    // clear keyboard state
    clear_keyboard();

    suspend_wakeup_init_quantum();
}
