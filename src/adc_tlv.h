#ifndef ADC_TLV_H_
#define ADC_TLV_H_

int encode(uint16_t pcount, uint8_t tag, uint8_t **data, int *len);
int decode(uint16_t *pcount, uint8_t *tag, uint8_t **data, int *len);

void make_tlv(uint8_t tag, uint8_t **data, int *len);

uint32_t crc32(uint8_t * buf, size_t len);
#endif
