#ifndef __TEST_CODE_H
#define __TEST_CODE_H

#include "stm32l4xx.h"                  // Device header
#include "internal_flash.h"
#include "zd25wq32.h"
#include "stdio.h"
#include "string.h"

void Test_InternalFlash(void);
void Test_MultiPage_And_Erase(void);

#endif
