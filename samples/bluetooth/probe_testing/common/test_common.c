#include "test_common.h"

void test_common_prepare_value(uint16_t id, uint8_t *descr, uint32_t sequence, uint8_t *dst)
{
	memset(dst, 0x00, MAX_DATA_LEN);
	snprintk(dst, MAX_DATA_LEN, "%03u-%s-%05u", id, descr, sequence);
}
