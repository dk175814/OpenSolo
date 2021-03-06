
#include "systime.h"
#include "sys.h"
#include "hw.h"
#include "gpio.h"

static const unsigned SYSTICK_HZ = (Sys::CPU_HZ / 8);
static const unsigned SYSTICK_IRQ_HZ = 50;
static const uint32_t SYSTICK_RELOAD = (SYSTICK_HZ / SYSTICK_IRQ_HZ) - 1;

/*
 * tickBase --
 *    Coarse timer, incremented once per rollover. Written only by the ISR. Read by anyone.
 *
 * lastTick --
 *    Last value returned from ticks(). Not used from interrupt context.
 *
 * heartbeat --
 *    User provided callback for periodic task execution.
 */
static SysTime::Ticks tickBase, lastTick;
static SysTime::HeartbeatCallback heartbeatCb;

static void emptyHeartbeat() {}

void SysTime::init(HeartbeatCallback hbcb)
{
    /*
     * The Cortex-M3 has a 24-bit hardware cycle timer, with an
     * interrupt and programmable reload.
     *
     * To use this timer to create a global monotonic timebase, we set
     * it up to roll over at a convenient rate (50Hz).  Those
     * rollovers increment a global counter, which we then combine
     * with the current timer value in ticks().
     */

    tickBase = 0;
    lastTick = 0;
    heartbeatCb = (hbcb == NULL) ? emptyHeartbeat : hbcb;

    NVIC.SysTick_CS = 0;
    NVIC.SysTick_RELOAD = SYSTICK_RELOAD;
    NVIC.SysTick = 0;

    // Enable timer, enable interrupt
    // NOTE: NVIC.SysTick gets loaded with NVIC.SysTick_RELOAD when 'enable' is applied
    NVIC.SysTick_CS = 3;
}

IRQ_HANDLER ISR_SysTick()
{
    tickBase += SysTime::hzTicks(SYSTICK_IRQ_HZ);

    heartbeatCb();
}

SysTime::Ticks SysTime::now()
{
    /*
     * Fractional part of our timebase (between IRQs)
     */
    uint32_t fractional = SYSTICK_RELOAD - NVIC.SysTick;
    Ticks t = tickBase;

    /*
     * We need to multiply 'fractional' by (hzTicks(SYSTICK_IRQ_HZ)) /
     * SYSTICK_RELOAD), in order to convert it from SysTick units to
     * Ticks (nanoseconds). Any integer approximation of this number
     * would give unacceptable error, and even any 32-bit fixed point
     * approximation.  So, we'll do a 32x64 fixed-point multiply. This
     * avoids a costly division, and keeps us on operations that the
     * Cortex-M3 implements in hardware.
     */

    t += (fractional * (uint64_t)(hzTicks(SYSTICK_IRQ_HZ) * 0x100000000ULL / SYSTICK_RELOAD)) >> 32;

    /*
     * Not monotonic? The timer must have rolled over, but we haven't
     * processed the rollover IRQ yet.
     */

    if (t < lastTick) {
        t += hzTicks(SYSTICK_IRQ_HZ);
    }
    lastTick = t;

    return t;
}
