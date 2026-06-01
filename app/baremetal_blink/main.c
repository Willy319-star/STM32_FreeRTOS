#include "board.h"

int main(void)
{
    Board_Init();

    while (1) {
        Board_LED_Toggle();
        HAL_Delay(500);
    }
}