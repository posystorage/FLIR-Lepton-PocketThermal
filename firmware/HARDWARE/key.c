#include "key.h"
#include "sys_tick.h"

// KEY1 PE13 - 电源键 / 长按关机，下拉读取，高电平按下
// KEY2 PC6  - 确认 / 快门
// KEY3 PA7  - LUT 菜单 / 取消
// KEY4 PA6  - 测温点菜单 / 右 / 下
// KEY5 PC8  - 发射率菜单 / 左 / 上

#define KEY_DEBOUNCE_MS    20   // 防抖时间 20ms
#define KEY_LONG_MS       2000  // 长按阈值 2s
#define KEY_REPEAT_MS     500   // 连发延迟 (预留)

/* 每键状态机 */
typedef enum {
    KS_IDLE,
    KS_DEBOUNCE,
    KS_PRESSED,
    KS_LONG,
} key_state_t;

typedef struct {
    key_state_t state;
    uint32_t    tick_start;  // 状态进入时的时间戳
    uint8_t     long_fired;  // 长按事件是否已触发
} key_machine_t;

static key_machine_t km[KEY_COUNT] = {0};
static uint8_t pin_read(uint8_t id);

/* ── 事件环形缓冲区 ── */
#define KEY_EVENT_BUF 16
static key_event_t evbuf[KEY_EVENT_BUF];
static uint8_t ev_r = 0, ev_w = 0;

static uint8_t ev_push(uint8_t id, key_event_type_t ev)
{
    uint8_t next = (ev_w + 1) % KEY_EVENT_BUF;
    if (next == ev_r) return 0; // 满
    evbuf[ev_w].key_id = id;
    evbuf[ev_w].event  = ev;
    ev_w = next;
    return 1;
}

/* ── 初始化 ── */
void Key_Init(void)
{
    /* KEY1 - PE13 */
    GPIO_SetMode(PE, BIT13, GPIO_MODE_INPUT);
    GPIO_SetPullCtl(PE, BIT13, GPIO_PUSEL_PULL_DOWN);

    /* KEY2 - PC6 */
    PC->MODE &= ~(0x3ul << (6 * 2));
    PC->PUSEL &= ~(0x3ul << (6 * 2));
    PC->PUSEL |= (1ul << (6 * 2));

    /* KEY3 - PA7 */
    PA->MODE &= ~(0x3ul << (7 * 2));
    PA->PUSEL &= ~(0x3ul << (7 * 2));
    PA->PUSEL |= (1ul << (7 * 2));

    /* KEY4 - PA6 */
    PA->MODE &= ~(0x3ul << (6 * 2));
    PA->PUSEL &= ~(0x3ul << (6 * 2));
    PA->PUSEL |= (1ul << (6 * 2));

    /* KEY5 - PC8 */
    GPIO_SetMode(PC, BIT8, GPIO_MODE_INPUT);
    GPIO_SetPullCtl(PC, BIT8, GPIO_PUSEL_PULL_UP);

}

/* ── 单次扫描 (主循环每 10ms 调用) ── */
void Key_Scan(void)
{
    uint32_t now = GetTick();
    uint8_t  id;

    for (id = 0; id < KEY_COUNT; id++)
    {
        uint8_t level = pin_read(id); // 0=按下
        key_machine_t *m = &km[id];

        switch (m->state)
        {
        case KS_IDLE:
            if (level == 0) {
                m->state = KS_DEBOUNCE;
                m->tick_start = now;
            }
            break;

        case KS_DEBOUNCE:
            if (level == 0) {
                if (now - m->tick_start >= KEY_DEBOUNCE_MS) {
                    // 防抖确认按下
                    m->state = KS_PRESSED;
                    m->tick_start = now;
                    m->long_fired = 0;
                    ev_push(id, KEY_EVENT_PRESS);
                }
            } else {
                m->state = KS_IDLE; // 抖动弹回
            }
            break;

        case KS_PRESSED:
            if (level == 0) {
                // 仍按住 — 检查长按
                if (!m->long_fired && (now - m->tick_start >= KEY_LONG_MS)) {
                    m->long_fired = 1;
                    ev_push(id, KEY_EVENT_LONG);
                }
            } else {
                // 释放
                m->state = KS_IDLE;
                ev_push(id, KEY_EVENT_RELEASE);
            }
            break;

        default:
            m->state = KS_IDLE;
            break;
        }
    }
}

/* ── 取出事件 (非阻塞) ── */
uint8_t Key_GetEvent(key_event_t *ev)
{
    if (ev_r == ev_w) return 0;
    *ev = evbuf[ev_r];
    ev_r = (ev_r + 1) % KEY_EVENT_BUF;
    return 1;
}

/* ── 读取引脚电平 ── */
static uint8_t pin_read(uint8_t id)
{
    switch (id)
    {
    case 0:  return PE13 ? 0u : 1u;
    case 1:  return PC6;
    case 2:  return PA7;
    case 3:  return PA6;
    case 4:  return PC8;
    default: return 1;
    }
}
