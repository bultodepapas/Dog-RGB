#include <stdio.h>
#include <string.h>
#include "qrcodegen.h"

/* Host research probe: encoder + arithmetic, not the LVGL renderer. */
int main(void) {
    const char *samples[] = {"https://wa.me/000000000000", "https://wa.me/000000000000000", "tel:+000000000000000"};
    const int widths[] = {100, 116, 132, 148};
    puts("payload_bytes,canvas,min_version,actual_modules,scale,left_margin,required_margin");
    for (unsigned j = 0; j < sizeof(samples)/sizeof(samples[0]); ++j) {
        size_t len = strlen(samples[j]);
        int minimum = qrcodegen_getMinFitVersion(qrcodegen_Ecc_MEDIUM, len);
        for (unsigned i = 0; i < sizeof(widths)/sizeof(widths[0]); ++i) {
            int width = widths[i];
            int n = qrcodegen_version2size(minimum);
            int scale = width / n;
            int version = minimum + (width % n) / (4 * scale);
            unsigned char tmp[qrcodegen_BUFFER_LEN_MAX], qr[qrcodegen_BUFFER_LEN_MAX];
            memcpy(tmp, samples[j], len);
            if (!qrcodegen_encodeBinary(tmp, len, qr, qrcodegen_Ecc_MEDIUM,
                version, version, qrcodegen_Mask_AUTO, true)) return 1;
            n = qrcodegen_getSize(qr);
            scale = width / n;
            printf("%zu,%d,%d,%d,%d,%d,%d\n", len, width, minimum, n, scale,
                (width - n * scale) / 2, 4 * scale);
        }
        unsigned char small_tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(3)];
        unsigned char small_qr[qrcodegen_BUFFER_LEN_FOR_VERSION(3)];
        memcpy(small_tmp, samples[j], len);
        if (!qrcodegen_encodeBinary(small_tmp, len, small_qr, qrcodegen_Ecc_MEDIUM,
            1, 3, qrcodegen_Mask_AUTO, true)) return 2;
        fprintf(stderr, "bounded payload=%zu modules=%d workspace=%zu bytes\n", len,
            qrcodegen_getSize(small_qr), sizeof(small_tmp) + sizeof(small_qr));
    }
    return 0;
}
