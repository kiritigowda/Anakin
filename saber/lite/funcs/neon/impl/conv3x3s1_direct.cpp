#include "saber/lite/funcs/neon/impl/conv_arm_impl.h"
#ifdef USE_ARM_PLACE

namespace anakin{

namespace saber{

namespace lite{


void conv_3x3s1_direct(const float* din, float* dout, \
                          int num, int chout, int hout, int wout, \
                          int chin, int hin, int win, \
                          const float* weights, const float* bias, \
                          int group, int kernel_w, int kernel_h, int stride_w, int stride_h, int dila_w, int dila_h, \
                          int pad_w, int pad_h, bool flag_bias, bool flag_relu, Sgemm& gemmer, void* work_space) {
    //! 3x3s1 convolution, implemented by direct algorithm
    //! pad is done implicit
    const float zero[6] = {0.f, 0.f, 0.f, 0.f, 0.f, 0.f};
    //! for 4x6 convolution window
    const int right_pad_idx[4] = {3, 2, 1, 0};

    int w_in = win;
    int h_in = hin;
    int ch_in = chin;

    int w_out = wout;
    int h_out = hout;
    int ch_out = chout;

    //const float* din = tensor_in.data();
    //float* dout = tensor_out.mutable_data();

    int size_in_channel = w_in * h_in;
    int size_out_channel = w_out * h_out;
    int w_stride = ch_in * 9;

    int tile_w = (w_in + 3) >> 2;
    int tile_h = (h_in + 1) >> 1;
    int w_in_twice = w_in << 1;
    int cnt_col = tile_w - 2;

    int size_pad_right = 1 + (tile_w << 2) - w_in;
    int size_pad_bottom = 1 + (tile_h << 1) - h_in;

    int cremain = ch_out - ((ch_out >> 1) << 1);

    uint32x4_t vmask_rp = vcgeq_s32(vld1q_s32(right_pad_idx), vdupq_n_s32(size_pad_right));
    unsigned int pmask_rp[4];
    vst1q_u32(pmask_rp, vmask_rp);
    int right_pad_sub = (size_pad_right - 1) * sizeof(float);

    for (int n = 0; n < num; ++n) {
        const float *din_batch = din + n * ch_in * size_in_channel;
        float *dout_batch = dout + n * ch_in * size_out_channel;
#pragma omp parallel for
        for (int c = 0; c < ch_out - 1; c += 2) {

            float* dout_c0 = dout_batch + c * size_out_channel;
            float* dout_c1 = dout_c0 + size_out_channel;

            if (flag_bias) {
                fill_bias(dout_c0, &bias[c], 1, size_out_channel);
                fill_bias(dout_c1, &bias[c + 1], 1, size_out_channel);
            } else {
                fill_bias(dout_c0, zero, 1, size_out_channel);
                fill_bias(dout_c1, zero, 1, size_out_channel);
            }

            //float* dout_c2 = dout_c1 + size_out_channel;
            //float* dout_c3 = dout_c2 + size_out_channel;

            const float* wc0 = weights + c * w_stride;
            const float* wc1 = wc0 + w_stride;

            //const float* wc2 = wc0 + w_stride;
            //const float* wc3 = wc0 + w_stride;

            for (int i = 0; i < ch_in; ++i) {

                int relu = 0;
                if ((i == ch_in - 1) && flag_relu) {
                    relu = 1;
                }

                const float *din_channel = din_batch + i * size_in_channel;

                const float* wcin0 = wc0 + i * 9;
                const float* wcin1 = wc1 + i * 9;
                float32x4_t wr00 = vld1q_f32(wcin0);
                float32x4_t wr01 = vld1q_f32(wcin0 + 3);
                float32x4_t wr02 = vld1q_f32(wcin0 + 6);

                float32x4_t wr10 = vld1q_f32(wcin1);
                float32x4_t wr11 = vld1q_f32(wcin1 + 3);
                float32x4_t wr12 = vld1q_f32(wcin1 + 6);

                float *doutc0r0 = dout_c0;
                float *doutc0r1 = doutc0r0 + w_out;

                float *doutc1r0 = dout_c1;
                float *doutc1r1 = doutc1r0 + w_out;

                const float *dr0 = din_channel;
                const float *dr1 = dr0 + w_in;
                const float *dr2 = dr1 + w_in;
                const float *dr3 = dr2 + w_in;

                const float *din0_ptr = dr0;
                const float *din1_ptr = dr1;
                const float *din2_ptr = dr2;
                const float *din3_ptr = dr3;

                float* ptr_zero = const_cast<float*>(zero);
                float32x4_t vzero = vdupq_n_f32(0.f);

                //! deal with top pad
                int h = 0;
                {
                    //! process
                    if (1) {
#ifdef __aarch64__
                        // todo
#else
                        int cnt = cnt_col;

                        //float tmp1[4];
                        //float* ptr1 = tmp1;
                        //float tmp2[4];
                        //float* ptr2 = tmp2;

                        asm volatile(
                        //! process left pad
                        "pld [%[doutc0r0]]                      @ preload data\n"
                                "pld [%[din0_ptr]]                      @ preload data\n"
                                "pld [%[doutc0r1]]                      @ preload data\n"
                                "pld [%[doutc1r0]]                      @ preload data\n"
                                "pld [%[doutc1r1]]                      @ preload data\n"
                                "vld1.32  {d26-d27}, [%[doutc0r0]]      @ load dout0r0\n"

                                //! 1st row
                                "vld1.32  {d20-d22}, [%[din0_ptr]]!     @ load din r1\n"
                                "vld1.32  {d28-d29}, [%[doutc0r1]]      @ load dout0r1\n"
                                "vld1.32  {d16-d17}, [%[doutc1r0]]      @ load dout1r0\n"
                                "vld1.32  {d18-d19}, [%[doutc1r1]]      @ load dout1r1\n"

                                "pld [%[din1_ptr]]                      @ preload data\n"
                                "pld [%[din2_ptr]]                      @ preload data\n"

                                "vext.32  q12, q10, q11, #1             @ shift left r1\n"
                                "vmla.f32 q13, q10, %e[wr01][1]         @ mul weight0 11, out0r0\n"
                                "vmla.f32 q14, q10, %e[wr00][1]         @ mul weight0 01, out0r1\n"
                                "vmla.f32 q8, q10,  %e[wr11][1]         @ mul weight1 11, out1r0\n"
                                "vmla.f32 q9, q10,  %e[wr10][1]         @ mul weight1 01, out1r1\n"

                                //"vmov.u32 q15, #0                       @ dump zero\n"
                                "vext.32  q15, %q[vzero], q10, #3       @ shift right r1\n"
                                "vmla.f32 q13, q12, %f[wr01][0]         @ mul weight0 12, out0r0\n"
                                "vmla.f32 q14, q12, %f[wr00][0]         @ mul weight0 02, out0r1\n"
                                "vmla.f32 q8, q12, %f[wr11][0]          @ mul weight1 12, out1r0\n"
                                "vmla.f32 q9, q12, %f[wr10][0]          @ mul weight1 02, out1r1\n"

                                "vld1.32  {d20-d22}, [%[din1_ptr]]!     @ load din r2\n"

                                "vmla.f32 q13, q15, %e[wr01][0]         @ mul weight0 10, out0r0\n"
                                "vmla.f32 q14, q15, %e[wr00][0]         @ mul weight0 00, out0r1\n"
                                "vmla.f32 q8, q15, %e[wr11][0]          @ mul weight1 10, out1r0\n"
                                "vmla.f32 q9, q15, %e[wr10][0]          @ mul weight1 00, out1r1\n"

                                "sub %[din0_ptr], #12                   @ 1pad + 2 float data overlap\n"


                                //! 2nd row
                                //"pld [%[din1_ptr]]                      @ preload data\n"
                                "vext.32  q12, q10, q11, #1             @ shift left r2\n"
                                "vmla.f32 q13, q10, %e[wr02][1]         @ mul weight0 21, out0r0\n"
                                "vmla.f32 q14, q10, %e[wr01][1]         @ mul weight0 11, out0r1\n"
                                "vmla.f32 q8, q10, %e[wr12][1]          @ mul weight1 21, out1r0\n"
                                "vmla.f32 q9, q10, %e[wr11][1]          @ mul weight1 11, out1r1\n"

                                "vext.32  q15, %q[vzero], q10, #3       @ shift right r2\n"
                                "vmla.f32 q13, q12, %f[wr02][0]         @ mul weight0 22, out0r0\n"
                                "vmla.f32 q14, q12, %f[wr01][0]         @ mul weight0 12, out0r1\n"
                                "vmla.f32 q8, q12, %f[wr12][0]          @ mul weight1 22, out1r0\n"
                                "vmla.f32 q9, q12, %f[wr11][0]          @ mul weight1 12, out1r1\n"

                                "vld1.32  {d20-d22}, [%[din2_ptr]]!     @ load din r3\n"

                                "vmla.f32 q13, q15, %e[wr02][0]         @ mul weight0 20, out0r0\n"
                                "vmla.f32 q14, q15, %e[wr01][0]         @ mul weight0 10, out0r1\n"
                                "vmla.f32 q8, q15, %e[wr12][0]          @ mul weight1 20, out1r0\n"
                                "vmla.f32 q9, q15, %e[wr11][0]          @ mul weight1 10, out1r1\n"

                                //! 3rd row
                                // "pld [%[din2_ptr]]                      @ preload data\n"
                                "vext.32  q12, q10, q11, #1             @ shift left r3\n"
                                "sub %[din1_ptr], #12                   @ 1pad + 2 float data overlap\n"
                                "vmla.f32 q14, q10, %e[wr02][1]         @ mul weight0 21, out0r1\n"
                                "vmla.f32 q9, q10, %e[wr12][1]          @ mul weight1 21, out1r1\n"

                                "vext.32  q15, %q[vzero], q10, #3               @ shift right r3\n"
                                "sub %[din2_ptr], #12                   @ 1pad + 2 float data overlap\n"
                                "vmla.f32 q14, q12, %f[wr02][0]         @ mul weight0 22, out0r1\n"
                                "vmla.f32 q9, q12, %f[wr12][0]          @ mul weight1 22, out1r1\n"

                                "pld [%[din0_ptr]]                      @ preload data\n"
                                "pld [%[din1_ptr]]                      @ preload data\n"
                                "pld [%[din2_ptr]]                      @ preload data\n"
                                "vmla.f32 q14, q15, %e[wr02][0]         @ mul weight0 20, out0r1\n"
                                "vmla.f32 q9, q15, %e[wr12][0]          @ mul weight1 20, out1r1\n"

                                //"sub %[din0_ptr], #12                   @ 1pad + 2 float data overlap\n"
                                // "sub %[din1_ptr], #12                   @ 1pad + 2 float data overlap\n"
                                //"sub %[din2_ptr], #12                   @ 1pad + 2 float data overlap\n"

                                "cmp %[relu], #1                        @ check whether has relu\n"
                                "blt    1f                              @ jump to top left store without relu\n"
                                "vmax.f32   q13, q13, %q[vzero]         @ relu\n"
                                "vmax.f32   q14, q14, %q[vzero]         @ relu\n"
                                "vmax.f32   q8, q8, %q[vzero]           @ relu\n"
                                "vmax.f32   q9, q9, %q[vzero]           @ relu\n"

                                "1:                              @ store top left result\n"
                                // stroe tl result
                                "vst1.32  {d26-d27}, [%[doutc0r0]]!     @ store result, add pointer\n"
                                "vst1.32  {d16-d17}, [%[doutc1r0]]!     @ store result, add pointer\n"

                                "vst1.32  {d28-d29}, [%[doutc0r1]]!     @ store result, add pointer\n"
                                "vst1.32  {d18-d19}, [%[doutc1r1]]!     @ store result, add pointer\n"

                                "pld [%[doutc0r0]]                      @ preload data\n"
                                "pld [%[doutc0r1]]                      @ preload data\n"
                                "pld [%[doutc1r0]]                      @ preload data\n"
                                "pld [%[doutc1r1]]                      @ preload data\n"

                                //! process mid cols
                                "cmp %[cnt], #1                         @ check whether has mid cols\n"
                                "blt  2f                                @ jump to main loop start point\n"
                                "start_top_mid:                         @ main loop start point\n"

                                "vld1.32  {d20-d22}, [%[din0_ptr]]!     @ load din r1\n"
                                "vld1.32  {d26-d27}, [%[doutc0r0]]      @ load dout0r0\n"
                                "vld1.32  {d28-d29}, [%[doutc0r1]]      @ load dout0r1\n"
                                "vld1.32  {d16-d17}, [%[doutc1r0]]      @ load dout1r0\n"
                                "vld1.32  {d18-d19}, [%[doutc1r1]]      @ load dout1r1\n"

                                "sub %[din0_ptr], #8                    @ 2 float data overlap with previous data\n"

                                //! 1st row
                                // "pld [%[din0_ptr]]                       @ preload data\n"
                                "vext.32  q12, q10, q11, #1             @ shift left r1\n"
                                "vmla.f32 q13, q10, %e[wr01][0]         @ mul weight0 10, out0r0\n"
                                "vmla.f32 q14, q10, %e[wr00][0]         @ mul weight0 00, out0r1\n"
                                "vmla.f32 q8, q10, %e[wr11][0]          @ mul weight1 10, out1r0\n"
                                "vmla.f32 q9, q10, %e[wr10][0]          @ mul weight1 00, out1r1\n"

                                "vext.32  q15, q10, q11, #2             @ shift left r1\n"
                                "vmla.f32 q13, q12, %e[wr01][1]         @ mul weight0 11, out0r0\n"
                                "vmla.f32 q14, q12, %e[wr00][1]         @ mul weight0 01, out0r1\n"
                                "vmla.f32 q8, q12, %e[wr11][1]          @ mul weight1 11, out1r0\n"
                                "vmla.f32 q9, q12, %e[wr10][1]          @ mul weight1 01, out1r1\n"

                                "vld1.32  {d20-d22}, [%[din1_ptr]]!     @ load din r2\n"
                                "vmla.f32 q13, q15, %f[wr01][0]         @ mul weight0 12, out0r0\n"
                                "vmla.f32 q14, q15, %f[wr00][0]         @ mul weight0 02, out0r1\n"
                                "vmla.f32 q8, q15, %f[wr11][0]          @ mul weight1 12, out1r0\n"
                                "vmla.f32 q9, q15, %f[wr10][0]          @ mul weight1 02, out1r1\n"

                                //! 2nd row
                                // "vld1.32  {d20-d22}, [%[din1_ptr]]!       @ load din r2\n"
                                //  "pld [%[din0_ptr]]                     @ preload data\n"
                                "sub %[din1_ptr], #8                    @ 2 float data overlap with previous data\n"

                                "vext.32  q12, q10, q11, #1             @ shift left r2\n"
                                "vmla.f32 q13, q10, %e[wr02][0]         @ mul weight0 20, out0r0\n"
                                "vmla.f32 q14, q10, %e[wr01][0]         @ mul weight0 10, out0r1\n"
                                "vmla.f32 q8, q10, %e[wr12][0]          @ mul weight1 20, out1r0\n"
                                "vmla.f32 q9, q10, %e[wr11][0]          @ mul weight1 10, out1r1\n"

                                "vext.32  q15, q10, q11, #2             @ shift left r2\n"
                                "vmla.f32 q13, q12, %e[wr02][1]         @ mul weight0 21, out0r0\n"
                                "vmla.f32 q14, q12, %e[wr01][1]         @ mul weight0 11, out0r1\n"
                                "vmla.f32 q8, q12, %e[wr12][1]          @ mul weight1 21, out1r0\n"
                                "vmla.f32 q9, q12, %e[wr11][1]          @ mul weight1 11, out1r1\n"

                                "vld1.32  {d20-d22}, [%[din2_ptr]]!     @ load din r3\n"
                                "vmla.f32 q13, q15, %f[wr02][0]         @ mul weight0 22, out0r0\n"
                                "vmla.f32 q14, q15, %f[wr01][0]         @ mul weight0 12, out0r1\n"
                                "vmla.f32 q8, q15, %f[wr12][0]          @ mul weight1 22, out1r0\n"
                                "vmla.f32 q9, q15, %f[wr11][0]          @ mul weight1 12, out1r1\n"

                                //! 3rd row
                                // "pld [%[din1_ptr]]                       @ preload data\n"
                                "sub %[din2_ptr], #8                    @ 2 float data overlap with previous data\n"

                                "vext.32  q12, q10, q11, #1             @ shift left r3\n"
                                "vmla.f32 q14, q10, %e[wr02][0]         @ mul weight0 20, out0r1\n"
                                "vmla.f32 q9, q10, %e[wr12][0]          @ mul weight1 20, out1r1\n"

                                "vext.32  q15, q10, q11, #2             @ shift left r3\n"
                                "pld [%[din0_ptr]]                      @ preload data\n"
                                "pld [%[din1_ptr]]                      @ preload data\n"
                                "vmla.f32 q14, q12, %e[wr02][1]         @ mul weight0 21, out0r1\n"
                                "vmla.f32 q9, q12, %e[wr12][1]          @ mul weight1 21, out1r1\n"

                                "pld [%[doutc0r0]]                      @ preload data\n"
                                "pld [%[doutc0r1]]                      @ preload data\n"

                                "pld [%[doutc1r0]]                      @ preload data\n"
                                "pld [%[doutc1r1]]                      @ preload data\n"

                                "vmla.f32 q14, q15, %f[wr02][0]         @ mul weight0 22, out0r1\n"
                                "vmla.f32 q9, q15, %f[wr12][0]          @ mul weight1 22, out1r1\n"

                                "cmp %[relu], #1                        @ check whether has relu\n"
                                "blt    3f                              @ jump to store without relu\n"
                                "vmax.f32   q13, q13, %q[vzero]         @ relu\n"
                                "vmax.f32   q14, q14, %q[vzero]         @ relu\n"
                                "vmax.f32   q8, q8, %q[vzero]           @ relu\n"
                                "vmax.f32   q9, q9, %q[vzero]           @ relu\n"

                                "3:                                     @ store top mid result\n"
                                // store tm result
                                "vst1.32  {d26-d27}, [%[doutc0r0]]!     @ store result, add pointer\n"
                                "vst1.32  {d16-d17}, [%[doutc1r0]]!     @ store result, add pointer\n"

                                "vst1.32  {d28-d29}, [%[doutc0r1]]!     @ store result, add pointer\n"
                                "vst1.32  {d18-d19}, [%[doutc1r1]]!     @ store result, add pointer\n"

                                // "sub %[din0_ptr], #8                    @ 2 float data overlap with previous data\n"
                                // "sub %[din1_ptr], #8                    @ 2 float data overlap with previous data\n"
                                // "sub %[din2_ptr], #8                    @ 2 float data overlap with previous data\n"
                                "subs %[cnt], #1                        @ loop count minus 1\n"
                                "bne    start_top_mid                   @ jump to main loop start point\n"

                                //! process right pad
                                "2:                                     @ right pad entry\n"
                                "vld1.32  {d20-d22}, [%[din0_ptr]]!     @ load din r1\n"
                                "vld1.32  {d26-d27}, [%[doutc0r0]]      @ load dout0r0\n"
                                "vld1.32  {d28-d29}, [%[doutc0r1]]      @ load dout0r1\n"
                                "vld1.32  {d16-d17}, [%[doutc1r0]]      @ load dout1r0\n"
                                "vld1.32  {d18-d19}, [%[doutc1r1]]      @ load dout1r1\n"

                                //! 1st row
                                "vbif d21, %e[vzero], %e[vmask_rp]      @ bit select, deal with right pad\n"
                                "vbif d22, %e[vzero], %f[vmask_rp]      @ bit select, deal with right pad\n"
                                "vext.32  q12, q10, q11, #1             @ shift left r1\n"
                                "vmla.f32 q13, q10, %e[wr01][0]         @ mul weight0 10, out0r0\n"
                                "vmla.f32 q14, q10, %e[wr00][0]         @ mul weight0 00, out0r1\n"
                                "vmla.f32 q8, q10, %e[wr11][0]          @ mul weight1 10, out1r0\n"
                                "vmla.f32 q9, q10, %e[wr10][0]          @ mul weight1 00, out1r1\n"

                                "vext.32  q15, q10, q11, #2             @ shift left r1\n"
                                "vmla.f32 q13, q12, %e[wr01][1]         @ mul weight0 11, out0r0\n"
                                "vmla.f32 q14, q12, %e[wr00][1]         @ mul weight0 01, out0r1\n"
                                "vmla.f32 q8, q12, %e[wr11][1]          @ mul weight1 11, out1r0\n"
                                "vmla.f32 q9, q12, %e[wr10][1]          @ mul weight1 01, out1r1\n"

                                "vld1.32  {d20-d22}, [%[din1_ptr]]!     @ load din r2\n"
                                "vmla.f32 q13, q15, %f[wr01][0]         @ mul weight0 12, out0r0\n"
                                "vmla.f32 q14, q15, %f[wr00][0]         @ mul weight0 02, out0r1\n"
                                "vmla.f32 q8, q15, %f[wr11][0]          @ mul weight1 12, out1r0\n"
                                "vmla.f32 q9, q15, %f[wr10][0]          @ mul weight1 02, out1r1\n"

                                //! 2nd row
                                "vbif d21, %e[vzero], %e[vmask_rp]      @ bit select, deal with right pad\n"
                                "vbif d22, %e[vzero], %f[vmask_rp]      @ bit select, deal with right pad\n"
                                "vext.32  q12, q10, q11, #1             @ shift left r2\n"
                                "vmla.f32 q13, q10, %e[wr02][0]         @ mul weight0 20, out0r0\n"
                                "vmla.f32 q14, q10, %e[wr01][0]         @ mul weight0 10, out0r1\n"
                                "vmla.f32 q8, q10, %e[wr12][0]          @ mul weight1 20, out1r0\n"
                                "vmla.f32 q9, q10, %e[wr11][0]          @ mul weight1 10, out1r1\n"

                                "vext.32  q15, q10, q11, #2             @ shift left r2\n"
                                "vmla.f32 q13, q12, %e[wr02][1]         @ mul weight0 21, out0r0\n"
                                "vmla.f32 q14, q12, %e[wr01][1]         @ mul weight0 11, out0r1\n"
                                "vmla.f32 q8, q12, %e[wr12][1]          @ mul weight1 21, out1r0\n"
                                "vmla.f32 q9, q12, %e[wr11][1]          @ mul weight1 11, out1r1\n"

                                "vld1.32  {d20-d22}, [%[din2_ptr]]!     @ load din r3\n"
                                "vmla.f32 q13, q15, %f[wr02][0]         @ mul weight0 22, out0r0\n"
                                "vmla.f32 q14, q15, %f[wr01][0]         @ mul weight0 12, out0r1\n"
                                "vmla.f32 q8, q15, %f[wr12][0]          @ mul weight1 22, out1r0\n"
                                "vmla.f32 q9, q15, %f[wr11][0]          @ mul weight1 12, out1r1\n"

                                //! 3rd row
                                "vbif d21, %e[vzero], %e[vmask_rp]      @ bit select, deal with right pad\n"
                                "vbif d22, %e[vzero], %f[vmask_rp]      @ bit select, deal with right pad\n"
                                "vext.32  q12, q10, q11, #1             @ shift left r3\n"
                                "vmla.f32 q14, q10, %e[wr02][0]         @ mul weight0 20, out0r1\n"
                                "vmla.f32 q9, q10, %e[wr12][0]          @ mul weight1 20, out1r1\n"

                                "vext.32  q15, q10, q11, #2             @ shift left r3\n"

                                "vmla.f32 q14, q12, %e[wr02][1]         @ mul weight0 21, out0r1\n"
                                "vmla.f32 q9, q12, %e[wr12][1]          @ mul weight1 21, out1r1\n"

                                "vld1.32  {d20-d21}, [%[doutc0r0]]      @ load dout0r0\n"
                                "vld1.32  {d22-d23}, [%[doutc0r1]]      @ load dout0r1\n"

                                "vmla.f32 q14, q15, %f[wr02][0]         @ mul weight0 22, out0r1\n"
                                "vmla.f32 q9, q15, %f[wr12][0]          @ mul weight1 22, out1r1\n"

                                "cmp %[relu], #1                        @ check whether has relu\n"
                                "blt    4f                              @ jump to store without relu\n"
                                "vmax.f32   q13, q13, %q[vzero]         @ relu\n"
                                "vmax.f32   q14, q14, %q[vzero]         @ relu\n"
                                "vmax.f32   q8, q8, %q[vzero]           @ relu\n"
                                "vmax.f32   q9, q9, %q[vzero]           @ relu\n"

                                "4:                                     @ store top mid result\n"
                                // store tr result

                                "vmvn.32  q12, %q[vzero]                @ \n"
                                "vext.32  q15, q12, %q[vmask_rp], #3    @ shift mask right 1\n"
                                "vbif q13, q10, q15                     @ bit select\n"
                                "vbif q14, q11, q15                     @ bit select\n"

                                "vld1.32  {d20-d21}, [%[doutc1r0]]      @ load dout1r0\n"
                                "vld1.32  {d22-d23}, [%[doutc1r1]]      @ load dout1r1\n"

                                "vbif q8, q10, q15                      @ bit select\n"
                                "vbif q9, q11, q15                      @ bit select\n"

                                "vst1.32  {d26-d27}, [%[doutc0r0]]!     @ store result, add pointer\n"
                                "vst1.32  {d28-d29}, [%[doutc0r1]]!     @ store result, add pointer\n"

                                "vst1.32  {d16-d17}, [%[doutc1r0]]!     @ store result, add pointer\n"
                                "vst1.32  {d18-d19}, [%[doutc1r1]]!     @ store result, add pointer\n"

                                "sub %[doutc1r0], %[doutc1r0], %[right_pad_sub] @ sub \n"
                                "sub %[doutc1r1], %[doutc1r1], %[right_pad_sub] @ sub \n"
                                "sub %[doutc0r0], %[doutc0r0], %[right_pad_sub] @ sub \n"
                                "sub %[doutc0r1], %[doutc0r1], %[right_pad_sub] @ sub \n"

                        :[doutc0r0] "+r"(doutc0r0), [doutc0r1] "+r"(doutc0r1), \
                            [doutc1r0] "+r" (doutc1r0), [doutc1r1] "+r" (doutc1r1),\
                            [din0_ptr] "+r"(din0_ptr), [din1_ptr] "+r"(din1_ptr), \
                            [din2_ptr] "+r"(din2_ptr), [cnt] "+r"(cnt)
                        :[wr00] "w"(wr00), [wr01] "w"(wr01), [wr02] "w"(wr02), \
                            [wr10] "w"(wr10), [wr11] "w"(wr11), [wr12] "w"(wr12), \
                            [vmask_rp] "w" (vmask_rp), [right_pad_sub] "r" (right_pad_sub), \
                            [vzero] "w" (vzero), [relu] "r" (relu)
                        :"q8", "q9", "q10", "q11", "q12", "q13", "q14", "q15"
                        );
#endif //__aarch64__
                    }
                    //! after process, increase pointer
                    doutc0r0 += w_out;
                    doutc0r1 = doutc0r0 + w_out;
                    doutc1r0 += w_out;
                    doutc1r1 = doutc1r0 + w_out;

                    dr0 = dr1;
                    dr1 = dr2;
                    dr2 = dr1 + w_in;
                    dr3 = dr2 + w_in;
                } //! end of process top row


                //! process mid row
                for (h = 1; h < tile_h - 1; h++) {
                    din0_ptr = dr0;
                    din1_ptr = dr1;
                    din2_ptr = dr2;
                    din3_ptr = dr3;

                    {
#ifdef __aarch64__
                        // todo
#else
                        int cnt = cnt_col;
                        asm volatile (
                        //! process left pad
                        "pld [%[doutc0r0]]                      @ preload data\n"
                                "pld [%[din0_ptr]]                      @ preload data\n"
                                "pld [%[doutc0r1]]                      @ preload data\n"
                                "pld [%[doutc1r0]]                      @ preload data\n"
                                "pld [%[doutc1r1]]                      @ preload data\n"
                                "vld1.32  {d26-d27}, [%[doutc0r0]]      @ load dout0r0\n"

                                //! 1st row
                                "vld1.32  {d20-d22}, [%[din0_ptr]]!     @ load din r1\n"
                                "vld1.32  {d16-d17}, [%[doutc1r0]]      @ load dout1r0\n"
                                "vld1.32  {d28-d29}, [%[doutc0r1]]      @ load dout0r1\n"
                                "vld1.32  {d18-d19}, [%[doutc1r1]]      @ load dout1r1\n"

                                "pld [%[din1_ptr]]                      @ preload data\n"
                                "pld [%[din2_ptr]]                      @ preload data\n"

                                //! 1st row
                                // "pld [%[din0_ptr], #192]             @ preload data\n"
                                "vext.32  q12, q10, q11, #1             @ shift left r0\n"
                                "vext.32  q15, %q[vzero], q10, #3       @ shift right r1\n"
                                "vmla.f32 q13, q10, %e[wr00][1]         @ mul weight0 01, out0r0\n"
                                "vmla.f32 q8, q10, %e[wr10][1]          @ mul weight1 01, out1r0\n"

                                "sub %[din0_ptr], #12                   @ 1pad + 2 float data overlap\n"
                                "vld1.32  {d20-d22}, [%[din1_ptr]]!     @ load din r1\n"


                                "vmla.f32 q13, q12, %f[wr00][0]         @ mul weight0 02, out0r0\n"
                                "vmla.f32 q8, q12, %f[wr10][0]          @ mul weight1 02, out1r0\n"

                                "pld [%[din0_ptr]]                      @ preload data\n"
                                "sub %[din1_ptr], #12                   @ 1pad + 2 float data overlap\n"

                                "vmla.f32 q13, q15, %e[wr00][0]         @ mul weight0 00, out0r0\n"
                                "vmla.f32 q8, q15, %e[wr10][0]          @ mul weight1 00, out1r0\n"

                                //! 2nd row
                                //"pld [%[din1_ptr], #192]              @ preload data\n"
                                "vext.32  q12, q10, q11, #1             @ shift left r1\n"
                                "vmla.f32 q13, q10, %e[wr01][1]         @ mul weight0 11, out0r0\n"
                                "vmla.f32 q14, q10, %e[wr00][1]         @ mul weight0 01, out0r1\n"
                                "vmla.f32 q8, q10, %e[wr11][1]          @ mul weight1 11, out1r0\n"
                                "vmla.f32 q9, q10, %e[wr10][1]          @ mul weight1 01, out1r1\n"

                                "vext.32  q15, %q[vzero], q10, #3       @ shift right r1\n"
                                "vmla.f32 q13, q12, %f[wr01][0]         @ mul weight0 12, out0r0\n"
                                "vmla.f32 q14, q12, %f[wr00][0]         @ mul weight0 02, out0r1\n"
                                "vmla.f32 q8, q12, %f[wr11][0]          @ mul weight1 12, out1r0\n"
                                "vmla.f32 q9, q12, %f[wr10][0]          @ mul weight1 02, out1r1\n"

                                "vld1.32  {d20-d22}, [%[din2_ptr]]!     @ load din r2\n"
                                "vmla.f32 q13, q15, %e[wr01][0]         @ mul weight0 10, out0r0\n"
                                "vmla.f32 q14, q15, %e[wr00][0]         @ mul weight0 00, out0r1\n"
                                "vmla.f32 q8, q15, %e[wr11][0]          @ mul weight1 10, out1r0\n"
                                "vmla.f32 q9, q15, %e[wr10][0]          @ mul weight1 00, out1r1\n"

                                //! 3rd row
                                //"pld [%[din2_ptr], #192]              @ preload data\n"
                                "vext.32  q12, q10, q11, #1             @ shift left r2\n"
                                "vmla.f32 q13, q10, %e[wr02][1]         @ mul weight0 21, out0r0\n"
                                "vmla.f32 q14, q10, %e[wr01][1]         @ mul weight0 11, out0r1\n"
                                "vmla.f32 q8, q10, %e[wr12][1]          @ mul weight1 21, out1r0\n"
                                "vmla.f32 q9, q10, %e[wr11][1]          @ mul weight1 11, out1r1\n"

                                "vext.32  q15, %q[vzero], q10, #3       @ shift right r2\n"
                                "vmla.f32 q13, q12, %f[wr02][0]         @ mul weight0 22, out0r0\n"
                                "vmla.f32 q14, q12, %f[wr01][0]         @ mul weight0 12, out0r1\n"
                                "vmla.f32 q8, q12, %f[wr12][0]          @ mul weight1 22, out1r0\n"
                                "vmla.f32 q9, q12, %f[wr11][0]          @ mul weight1 12, out1r1\n"

                                "vld1.32  {d20-d22}, [%[din3_ptr]]!     @ load din r3\n"
                                "vmla.f32 q13, q15, %e[wr02][0]         @ mul weight0 20, out0r0\n"
                                "vmla.f32 q14, q15, %e[wr01][0]         @ mul weight0 10, out0r1\n"
                                "vmla.f32 q8, q15, %e[wr12][0]          @ mul weight1 20, out1r0\n"
                                "vmla.f32 q9, q15, %e[wr11][0]          @ mul weight1 10, out1r1\n"

                                //! 4rd row
                                //"pld [%[din3_ptr], #192]              @ preload data\n"
                                "pld [%[din1_ptr]]                      @ preload data\n"
                                "vext.32  q12, q10, q11, #1             @ shift left r3\n"
                                "sub %[din2_ptr], #12                   @ 1pad + 2 float data overlap\n"
                                "vmla.f32 q14, q10, %e[wr02][1]         @ mul weight0 21, out0r1\n"
                                "vmla.f32 q9, q10, %e[wr12][1]          @ mul weight1 21, out1r1\n"

                                "vext.32  q15, %q[vzero], q10, #3       @ shift right r3\n"
                                "pld [%[din2_ptr]]                      @ preload data\n"
                                "sub %[din3_ptr], #12                   @ 1pad + 2 float data overlap\n"

                                "vmla.f32 q14, q12, %f[wr02][0]         @ mul weight0 22, out0r1\n"
                                "vmla.f32 q9, q12, %f[wr12][0]          @ mul weight1 22, out1r1\n"

                                "pld [%[doutc0r0], #128]                @ preload data\n"
                                "pld [%[doutc1r0], #128]                @ preload data\n"

                                "vmla.f32 q14, q15, %e[wr02][0]         @ mul weight0 20, out0r1\n"
                                "vmla.f32 q9, q15, %e[wr12][0]          @ mul weight1 20, out1r1\n"

                                "cmp %[relu], #1                        @ check whether has relu\n"
                                "blt    5f                              @ jump to store without relu\n"
                                "vmax.f32   q13, q13, %q[vzero]         @ relu\n"
                                "vmax.f32   q8, q8, %q[vzero]           @ relu\n"
                                "vmax.f32   q14, q14, %q[vzero]         @ relu\n"
                                "vmax.f32   q9, q9, %q[vzero]           @ relu\n"

                                "5:                                     @ store top mid result\n"
                                // store ml result
                                "pld [%[din3_ptr]]                      @ preload data\n"
                                "vst1.32  {d26-d27}, [%[doutc0r0]]!     @ store result, add pointer\n"
                                "vst1.32  {d16-d17}, [%[doutc1r0]]!     @ store result, add pointer\n"

                                "vst1.32  {d28-d29}, [%[doutc0r1]]!     @ store result, add pointer\n"
                                "vst1.32  {d18-d19}, [%[doutc1r1]]!     @ store result, add pointer\n"

                                "pld [%[doutc0r1]]                      @ preload data\n"
                                "pld [%[doutc1r1]]                      @ preload data\n"

                                //! process mid cols
                                "cmp %[cnt], #1                         @ check whether has mid cols\n"
                                "blt  6f                                @ jump to main loop start point\n"
                                "start_mid_mid:                         @ main loop start point\n"

                                "vld1.32  {d20-d22}, [%[din0_ptr]]!     @ load din r0\n"
                                "vld1.32  {d26-d27}, [%[doutc0r0]]      @ load dout0r0\n"
                                "vld1.32  {d16-d17}, [%[doutc1r0]]      @ load dout1r0\n"
                                "vld1.32  {d28-d29}, [%[doutc0r1]]      @ load dout0r1\n"
                                "vld1.32  {d18-d19}, [%[doutc1r1]]      @ load dout1r1\n"

                                //! 1st row
                                // "pld [%[din0_ptr], #192]                @ preload data\n"
                                "vext.32  q12, q10, q11, #1             @ shift left r0\n"
                                "vmla.f32 q13, q10, %e[wr00][0]         @ mul weight0 00, out0r0\n"
                                "vmla.f32 q8, q10, %e[wr10][0]          @ mul weight1 00, out1r0\n"


                                "vext.32  q15, q10, q11, #2             @ shift left r0\n"
                                "sub %[din0_ptr], #8                    @ 2 float data overlap with previous data\n"

                                "vmla.f32 q13, q12, %e[wr00][1]         @ mul weight0 01, out0r0\n"
                                "vmla.f32 q8, q12, %e[wr10][1]          @ mul weight1 01, out1r0\n"

                                "vld1.32  {d20-d22}, [%[din1_ptr]]!     @ load din r1\n"
                                "pld [%[din0_ptr]]                      @ preload data\n"
                                "sub %[din1_ptr], #8                    @ 2 float data overlap with previous data\n"

                                "vmla.f32 q13, q15, %f[wr00][0]         @ mul weight0 02, out0r0\n"
                                "vmla.f32 q8, q15, %f[wr10][0]          @ mul weight1 02, out1r0\n"

                                //! 2nd row
                                "pld [%[din1_ptr]]                      @ preload data\n"
                                "vext.32  q12, q10, q11, #1             @ shift left r1\n"
                                "vmla.f32 q14, q10, %e[wr00][0]         @ mul weight0 00, out0r1\n"
                                "vmla.f32 q9, q10, %e[wr10][0]          @ mul weight1 00, out1r1\n"
                                "vmla.f32 q13, q10, %e[wr01][0]         @ mul weight0 10, out0r0\n"
                                "vmla.f32 q8, q10, %e[wr11][0]          @ mul weight1 10, out1r0\n"

                                "vext.32  q15, q10, q11, #2             @ shift left r1\n"
                                "vmla.f32 q14, q12, %e[wr00][1]         @ mul weight0 01, out0r1\n"
                                "vmla.f32 q9, q12, %e[wr10][1]          @ mul weight1 01, out1r1\n"
                                "vmla.f32 q13, q12, %e[wr01][1]         @ mul weight0 11, out0r0\n"
                                "vmla.f32 q8, q12, %e[wr11][1]          @ mul weight1 11, out1r0\n"

                                "vld1.32  {d20-d22}, [%[din2_ptr]]!     @ load din r2\n"
                                "vmla.f32 q14, q15, %f[wr00][0]         @ mul weight0 02, out0r1\n"
                                "vmla.f32 q9, q15, %f[wr10][0]          @ mul weight1 02, out1r1\n"
                                "vmla.f32 q13, q15, %f[wr01][0]         @ mul weight0 12, out0r0\n"
                                "vmla.f32 q8, q15, %f[wr11][0]          @ mul weight1 12, out1r0\n"

                                //! 3rd row
                                "sub %[din2_ptr], #8                    @ 2 float data overlap with previous data\n"
                                "vext.32  q12, q10, q11, #1             @ shift left r2\n"
                                "vmla.f32 q14, q10, %e[wr01][0]         @ mul weight0 10, out0r1\n"
                                "vmla.f32 q9, q10, %e[wr11][0]          @ mul weight1 10, out1r1\n"
                                "vmla.f32 q13, q10, %e[wr02][0]         @ mul weight0 20, out0r0\n"
                                "vmla.f32 q8, q10, %e[wr12][0]          @ mul weight1 20, out1r0\n"

                                "vext.32  q15, q10, q11, #2             @ shift right r2\n"
                                "vmla.f32 q14, q12, %e[wr01][1]         @ mul weight0 11, out0r1\n"
                                "vmla.f32 q9, q12, %e[wr11][1]          @ mul weight1 11, out1r1\n"
                                "vmla.f32 q13, q12, %e[wr02][1]         @ mul weight0 21, out0r0\n"
                                "vmla.f32 q8, q12, %e[wr12][1]          @ mul weight1 21, out1r0\n"

                                "vld1.32  {d20-d22}, [%[din3_ptr]]!     @ load din r3\n"
                                "vmla.f32 q14, q15, %f[wr01][0]         @ mul weight0 12, out0r1\n"
                                "vmla.f32 q9, q15, %f[wr11][0]          @ mul weight1 12, out1r1\n"
                                "vmla.f32 q13, q15, %f[wr02][0]         @ mul weight0 22, out0r0\n"
                                "vmla.f32 q8, q15, %f[wr12][0]          @ mul weight1 22, out1r0\n"

                                //! 4rd row
                                "pld [%[din2_ptr]]                      @ preload data\n"
                                "vext.32  q12, q10, q11, #1             @ shift left r3\n"
                                "vmla.f32 q14, q10, %e[wr02][0]         @ mul weight0 20, out0r1\n"
                                "vmla.f32 q9, q10, %e[wr12][0]          @ mul weight1 20, out1r1\n"

                                "sub %[din3_ptr], #8                    @ 2 float data overlap with previous data\n"
                                "vext.32  q15, q10, q11, #2             @ shift left r3\n"
                                "vmla.f32 q14, q12, %e[wr02][1]         @ mul weight0 21, out0r1\n"
                                "vmla.f32 q9, q12, %e[wr12][1]          @ mul weight1 21, out1r1\n"

                                "pld [%[doutc0r0], #128]                @ preload data\n"
                                "pld [%[doutc1r0], #128]                @ preload data\n"

                                "vmla.f32 q14, q15, %f[wr02][0]         @ mul weight0 22, out0r1\n"
                                "vmla.f32 q9, q15, %f[wr12][0]          @ mul weight1 22, out1r1\n"

                                "cmp %[relu], #1                        @ check whether has relu\n"
                                "blt    7f                              @ jump to store without relu\n"
                                "vmax.f32   q13, q13, %q[vzero]         @ relu\n"
                                "vmax.f32   q8, q8, %q[vzero]           @ relu\n"
                                "vmax.f32   q14, q14, %q[vzero]         @ relu\n"
                                "vmax.f32   q9, q9, %q[vzero]           @ relu\n"

                                "7:                                     @ store top mid result\n"
                                // store ml result
                                "pld [%[din3_ptr]]                      @ preload data\n"
                                "vst1.32  {d26-d27}, [%[doutc0r0]]!     @ store result, add pointer\n"
                                "vst1.32  {d16-d17}, [%[doutc1r0]]!     @ store result, add pointer\n"

                                "vst1.32  {d28-d29}, [%[doutc0r1]]!     @ store result, add pointer\n"
                                "vst1.32  {d18-d19}, [%[doutc1r1]]!     @ store result, add pointer\n"

                                "pld [%[doutc0r1]]                      @ preload data\n"
                                "pld [%[doutc1r1]]                      @ preload data\n"

                                "subs %[cnt], #1                        @ loop count minus 1\n"
                                "bne    start_mid_mid                   @ jump to main loop start point\n"

                                //! process right pad
                                "6:                                     @ right pad entry\n"

                                "vld1.32  {d20-d22}, [%[din0_ptr]]!     @ load din r0\n"
                                "vld1.32  {d26-d27}, [%[doutc0r0]]      @ load dout0r0\n"
                                "vld1.32  {d16-d17}, [%[doutc1r0]]      @ load dout1r0\n"
                                "vld1.32  {d28-d29}, [%[doutc0r1]]      @ load dout0r1\n"
                                "vld1.32  {d18-d19}, [%[doutc1r1]]      @ load dout1r1\n"

                                //! 1st row
                                "vbif d21, %e[vzero], %e[vmask_rp]      @ bit select, deal with right pad\n"
                                "vbif d22, %e[vzero], %f[vmask_rp]      @ bit select, deal with right pad\n"
                                "vext.32  q12, q10, q11, #1             @ shift left r0\n"
                                "vmla.f32 q13, q10, %e[wr00][0]         @ mul weight0 00, out0r0\n"
                                "vmla.f32 q8, q10, %e[wr10][0]          @ mul weight1 00, out1r0\n"

                                "vext.32  q15, q10, q11, #2             @ shift left r0\n"
                                "vmla.f32 q13, q12, %e[wr00][1]         @ mul weight0 01, out0r0\n"
                                "vmla.f32 q8, q12, %e[wr10][1]          @ mul weight1 01, out1r0\n"

                                "vld1.32  {d20-d22}, [%[din1_ptr]]!     @ load din r1\n"
                                "vmla.f32 q13, q15, %f[wr00][0]         @ mul weight0 02, out0r0\n"
                                "vmla.f32 q8, q15, %f[wr10][0]          @ mul weight1 02, out1r0\n"

                                //! 2nd row
                                "vbif d21, %e[vzero], %e[vmask_rp]      @ bit select, deal with right pad\n"
                                "vbif d22, %e[vzero], %f[vmask_rp]      @ bit select, deal with right pad\n"
                                "vext.32  q12, q10, q11, #1             @ shift left r1\n"
                                "vmla.f32 q14, q10, %e[wr00][0]         @ mul weight0 00, out0r1\n"
                                "vmla.f32 q9, q10, %e[wr10][0]          @ mul weight1 00, out1r1\n"
                                "vmla.f32 q13, q10, %e[wr01][0]         @ mul weight0 10, out0r0\n"
                                "vmla.f32 q8, q10, %e[wr11][0]          @ mul weight1 10, out1r0\n"

                                "vext.32  q15, q10, q11, #2             @ shift left r1\n"
                                "vmla.f32 q14, q12, %e[wr00][1]         @ mul weight0 01, out0r1\n"
                                "vmla.f32 q9, q12, %e[wr10][1]          @ mul weight1 01, out1r1\n"
                                "vmla.f32 q13, q12, %e[wr01][1]         @ mul weight0 11, out0r0\n"
                                "vmla.f32 q8, q12, %e[wr11][1]          @ mul weight1 11, out1r0\n"

                                "vld1.32  {d20-d22}, [%[din2_ptr]]!     @ load din r2\n"
                                "vmla.f32 q14, q15, %f[wr00][0]         @ mul weight0 02, out0r1\n"
                                "vmla.f32 q9, q15, %f[wr10][0]          @ mul weight1 02, out1r1\n"
                                "vmla.f32 q13, q15, %f[wr01][0]         @ mul weight0 12, out0r0\n"
                                "vmla.f32 q8, q15, %f[wr11][0]          @ mul weight1 12, out1r0\n"

                                //! 3rd row
                                "vbif d21, %e[vzero], %e[vmask_rp]      @ bit select, deal with right pad\n"
                                "vbif d22, %e[vzero], %f[vmask_rp]      @ bit select, deal with right pad\n"
                                "vext.32  q12, q10, q11, #1             @ shift left r2\n"
                                "vmla.f32 q14, q10, %e[wr01][0]         @ mul weight0 10, out0r1\n"
                                "vmla.f32 q9, q10, %e[wr11][0]          @ mul weight1 10, out1r1\n"
                                "vmla.f32 q13, q10, %e[wr02][0]         @ mul weight0 20, out0r0\n"
                                "vmla.f32 q8, q10, %e[wr12][0]          @ mul weight1 20, out1r0\n"

                                "vext.32  q15, q10, q11, #2             @ shift right r2\n"
                                "vmla.f32 q14, q12, %e[wr01][1]         @ mul weight0 11, out0r1\n"
                                "vmla.f32 q9, q12, %e[wr11][1]          @ mul weight1 11, out1r1\n"
                                "vmla.f32 q13, q12, %e[wr02][1]         @ mul weight0 21, out0r0\n"
                                "vmla.f32 q8, q12, %e[wr12][1]          @ mul weight1 21, out1r0\n"

                                "vld1.32  {d20-d22}, [%[din3_ptr]]!     @ load din r3\n"
                                "vmla.f32 q14, q15, %f[wr01][0]         @ mul weight0 12, out0r1\n"
                                "vmla.f32 q9, q15, %f[wr11][0]          @ mul weight1 12, out1r1\n"
                                "vmla.f32 q13, q15, %f[wr02][0]         @ mul weight0 22, out0r0\n"
                                "vmla.f32 q8, q15, %f[wr12][0]          @ mul weight1 22, out1r0\n"

                                //! 4rd row
                                "vbif d21, %e[vzero], %e[vmask_rp]      @ bit select, deal with right pad\n"
                                "vbif d22, %e[vzero], %f[vmask_rp]      @ bit select, deal with right pad\n"
                                "vext.32  q12, q10, q11, #1             @ shift left r3\n"
                                "vmla.f32 q14, q10, %e[wr02][0]         @ mul weight0 20, out0r1\n"
                                "vmla.f32 q9, q10, %e[wr12][0]          @ mul weight1 20, out1r1\n"

                                "vext.32  q15, q10, q11, #2             @ shift left r3\n"
                                "vmla.f32 q14, q12, %e[wr02][1]         @ mul weight0 21, out0r1\n"
                                "vmla.f32 q9, q12, %e[wr12][1]          @ mul weight1 21, out1r1\n"

                                "vld1.32  {d20-d21}, [%[doutc0r0]]      @ load dout0r0\n"
                                "vld1.32  {d22-d23}, [%[doutc1r0]]      @ load dout1r0\n"

                                "vmla.f32 q14, q15, %f[wr02][0]         @ mul weight0 22, out0r1\n"
                                "vmla.f32 q9, q15, %f[wr12][0]          @ mul weight1 22, out1r1\n"

                                "cmp %[relu], #1                        @ check whether has relu\n"
                                "blt    8f                              @ jump to store without relu\n"
                                "vmax.f32   q13, q13, %q[vzero]         @ relu\n"
                                "vmax.f32   q8, q8, %q[vzero]           @ relu\n"
                                "vmax.f32   q14, q14, %q[vzero]         @ relu\n"
                                "vmax.f32   q9, q9, %q[vzero]           @ relu\n"

                                "8:                                     @ store mid right result\n"
                                // store mr result
                                "vmvn.32  q12, %q[vzero]                @ \n"
                                "vext.32  q15, q12, %q[vmask_rp], #3    @ shift mask right 1\n"
                                "vbif q13, q10, q15                     @ bit select\n"
                                "vbif q8, q11, q15                      @ bit select\n"

                                "vld1.32  {d20-d21}, [%[doutc0r1]]      @ load dout0r1\n"
                                "vld1.32  {d22-d23}, [%[doutc1r1]]      @ load dout1r1\n"
                                "vbif q14, q10, q15                     @ bit select\n"
                                "vbif q9, q11, q15                      @ bit select\n"

                                "vst1.32  {d26-d27}, [%[doutc0r0]]!     @ store result, add pointer\n"
                                "vst1.32  {d16-d17}, [%[doutc1r0]]!     @ store result, add pointer\n"

                                "vst1.32  {d18-d19}, [%[doutc1r1]]!     @ store result, add pointer\n"
                                "vst1.32  {d28-d29}, [%[doutc0r1]]!     @ store result, add pointer\n"

                                "sub %[doutc1r1], %[doutc1r1], %[right_pad_sub] @ sub \n"
                                "sub %[doutc0r1], %[doutc0r1], %[right_pad_sub] @ sub \n"
                                "sub %[doutc0r0], %[doutc0r0], %[right_pad_sub] @ sub \n"
                                "sub %[doutc1r0], %[doutc1r0], %[right_pad_sub] @ sub \n"

                        :[doutc0r0] "+r"(doutc0r0), [doutc0r1] "+r"(doutc0r1), \
                            [doutc1r0] "+r" (doutc1r0), [doutc1r1] "+r" (doutc1r1),\
                            [din0_ptr] "+r"(din0_ptr), [din1_ptr] "+r"(din1_ptr), \
                            [din2_ptr] "+r"(din2_ptr), [din3_ptr] "+r"(din3_ptr), \
                            [cnt] "+r"(cnt)
                        :[wr00] "w"(wr00), [wr01] "w"(wr01), [wr02] "w"(wr02), \
                            [wr10] "w"(wr10), [wr11] "w"(wr11), [wr12] "w"(wr12), \
                            [vmask_rp] "w" (vmask_rp), [right_pad_sub] "r" (right_pad_sub), \
                            [vzero] "w" (vzero), [relu] "r" (relu)
                        :"q8", "q9", "q10", "q11", "q12", "q13", "q14", "q15"
                        );
#endif //__aarch64__
                    }
                    doutc0r0 += w_out;
                    doutc0r1 = doutc0r0 + w_out;
                    doutc1r0 += w_out;
                    doutc1r1 = doutc1r0 + w_out;

                    dr0 = dr2;
                    dr1 = dr3;
                    dr2 = dr1 + w_in;
                    dr3 = dr2 + w_in;
                } //! end of processing mid rows

                //! deal with bottom pad
                if (1) {

                    din0_ptr = dr0;
                    din1_ptr = dr1;
                    if (size_pad_bottom == 2) {
#ifdef __aarch64__
                        // todo
#else
                        int cnt = cnt_col;
                        asm volatile (
                        //! process left pad
                        "pld [%[din0_ptr]]                      @ preload data\n"
                                "pld [%[doutc0r0]]                      @ preload data\n"
                                "pld [%[doutc1r0]]                      @ preload data\n"
                                "pld [%[din1_ptr]]                      @ preload data\n"
                                "vld1.32  {d16-d18}, [%[din0_ptr]]!     @ load din r0\n"
                                "vld1.32  {d12-d13}, [%[doutc0r0]]      @ load dout0r0\n"
                                "vld1.32  {d14-d15}, [%[doutc1r0]]      @ load dout1r0\n"

                                //! 1st row
                                "vld1.32  {d20-d22}, [%[din1_ptr]]!     @ load din r1\n"

                                "vext.32  q12, q8, q9, #1               @ shift left r0\n"
                                "vmla.f32 q6, q8, %e[wr00][1]           @ mul weight0 01, out0r0\n"
                                "vmla.f32 q7, q8, %e[wr10][1]           @ mul weight1 01, out1r0\n"

                                "sub %[din0_ptr], #12                   @ 1pad + 2 float data overlap\n"
                                //"pld [%[din0_ptr]]                      @ preload data\n"
                                "vext.32  q15, %q[vzero], q8, #3              @ shift right r1\n"

                                "vmla.f32 q6, q12, %f[wr00][0]          @ mul weight0 02, out0r0\n"
                                "vmla.f32 q7, q12, %f[wr10][0]          @ mul weight1 02, out1r0\n"

                                "sub %[din1_ptr], #12                   @ 1pad + 2 float data overlap\n"
                                "pld [%[din0_ptr]]                      @ preload data\n"
                                "vmla.f32 q6, q15, %e[wr00][0]          @ mul weight0 00, out0r0\n"
                                "vmla.f32 q7, q15, %e[wr10][0]          @ mul weight1 00, out1r0\n"

                                //! 2nd row
                                "pld [%[din1_ptr]]                      @ preload data\n"
                                "vext.32  q12, q10, q11, #1             @ shift left r1\n"
                                "vmla.f32 q6, q10, %e[wr01][1]          @ mul weight0 11, out0r0\n"
                                "vmla.f32 q7, q10, %e[wr11][1]          @ mul weight1 11, out1r0\n"

                                "vext.32  q15, %q[vzero], q10, #3             @ shift right r1\n"
                                "vmla.f32 q6, q12, %f[wr01][0]          @ mul weight0 12, out0r0\n"
                                "vmla.f32 q7, q12, %f[wr11][0]          @ mul weight1 12, out1r0\n"

                                "vmla.f32 q6, q15, %e[wr01][0]          @ mul weight0 10, out0r0\n"
                                "vmla.f32 q7, q15, %e[wr11][0]          @ mul weight1 10, out1r0\n"

                                "cmp %[relu], #1                        @ check whether has relu\n"
                                "blt    9f                              @ jump to store without relu\n"
                                "vmax.f32   q6, q6, %q[vzero]           @ relu\n"
                                "vmax.f32   q7, q7, %q[vzero]           @ relu\n"

                                "9:                                     @ store mid right result\n"
                                // store bl1 result
                                "vst1.32  {d12-d13}, [%[doutc0r0]]!     @ store result, add pointer\n"
                                "vst1.32  {d14-d15}, [%[doutc1r0]]!     @ store result, add pointer\n"
                                "pld [%[doutc0r0]]                      @ preload data\n"
                                "pld [%[doutc1r0]]                      @ preload data\n"

                                //! process mid cols
                                "cmp %[cnt], #1                         @ check whether has mid cols\n"
                                "blt  10f                               @ jump to main loop start point\n"
                                "12:                                    @ main loop start point\n"

                                "vld1.32  {d16-d18}, [%[din0_ptr]]!     @ load din r0\n"
                                "vld1.32  {d12-d13}, [%[doutc0r0]]      @ load dout0r0\n"
                                "vld1.32  {d14-d15}, [%[doutc1r0]]      @ load dout1r0\n"
                                "vld1.32  {d20-d22}, [%[din1_ptr]]!     @ load din r1\n"

                                "sub %[din0_ptr], #8                    @ 2 float data overlap with previous data\n"
                                //"sub %[din1_ptr], #8                    @ 2 float data overlap with previous data\n"
                                //! 1st row
                                "vext.32  q12, q8, q9, #1               @ shift left r0\n"
                                "vmla.f32 q6, q8, %e[wr00][0]           @ mul weight0 00, out0r0\n"
                                "vmla.f32 q7, q8, %e[wr10][0]           @ mul weight1 00, out1r0\n"

                                "vext.32  q15, q8, q9, #2               @ shift left r0\n"
                                "pld [%[din0_ptr]]                      @ preload data\n"
                                "sub %[din1_ptr], #8                    @ 2 float data overlap with previous data\n"

                                "vmla.f32 q6, q12, %e[wr00][1]          @ mul weight0 01, out0r0\n"
                                "vmla.f32 q7, q12, %e[wr10][1]          @ mul weight1 01, out1r0\n"

                                "pld [%[din1_ptr]]                      @ preload data\n"
                                "vmla.f32 q6, q15, %f[wr00][0]          @ mul weight0 02, out0r0\n"
                                "vmla.f32 q7, q15, %f[wr10][0]          @ mul weight1 02, out1r0\n"

                                //! 2nd row
                                "vext.32  q12, q10, q11, #1             @ shift left r1\n"
                                "vmla.f32 q6, q10, %e[wr01][0]          @ mul weight0 10, out0r0\n"
                                "vmla.f32 q7, q10, %e[wr11][0]          @ mul weight1 10, out1r0\n"

                                "vext.32  q15, q10, q11, #2             @ shift left r1\n"
                                "vmla.f32 q6, q12, %e[wr01][1]          @ mul weight0 11, out0r0\n"
                                "vmla.f32 q7, q12, %e[wr11][1]          @ mul weight1 11, out1r0\n"

                                "vmla.f32 q6, q15, %f[wr01][0]          @ mul weight0 12, out0r0\n"
                                "vmla.f32 q7, q15, %f[wr11][0]          @ mul weight1 12, out1r0\n"

                                "cmp %[relu], #1                        @ check whether has relu\n"
                                "blt    11f                             @ jump to store without relu\n"
                                "vmax.f32   q6, q6, %q[vzero]           @ relu\n"
                                "vmax.f32   q7, q7, %q[vzero]           @ relu\n"

                                "11:                                    @ store mid right result\n"
                                // store bm1 result

                                "vst1.32  {d12-d13}, [%[doutc0r0]]!     @ store result, add pointer\n"
                                "vst1.32  {d14-d15}, [%[doutc1r0]]!     @ store result, add pointer\n"
                                "pld [%[doutc0r0]]                      @ preload data\n"
                                "pld [%[doutc1r0]]                      @ preload data\n"

                                "subs %[cnt], #1                        @ loop count minus 1\n"
                                "bne    12b                             @ jump to main loop start point\n"

                                //! process right pad
                                "10:                                   @ right pad entry\n"

                                "vld1.32  {d16-d18}, [%[din0_ptr]]!     @ load din r0\n"
                                "vld1.32  {d20-d22}, [%[din1_ptr]]!     @ load din r1\n"
                                "vld1.32  {d12-d13}, [%[doutc0r0]]      @ load dout0r0\n"
                                "vld1.32  {d14-d15}, [%[doutc1r0]]      @ load dout1r0\n"

                                //! 1st row
                                "vbif d17, %e[vzero], %e[vmask_rp]      @ bit select, deal with right pad\n"
                                "vbif d18, %e[vzero], %f[vmask_rp]      @ bit select, deal with right pad\n"
                                "vext.32  q12, q8, q9, #1               @ shift left r0\n"

                                "vmla.f32 q6, q8, %e[wr00][0]           @ mul weight0 00, out0r0\n"
                                "vmla.f32 q7, q8, %e[wr10][0]           @ mul weight1 00, out1r0\n"

                                "vext.32  q15, q8, q9, #2               @ shift left r0\n"
                                "vmla.f32 q6, q12, %e[wr00][1]          @ mul weight0 01, out0r0\n"
                                "vmla.f32 q7, q12, %e[wr10][1]          @ mul weight1 01, out1r0\n"

                                "vbif d21, %e[vzero], %e[vmask_rp]      @ bit select, deal with right pad\n"
                                "vbif d22, %e[vzero], %f[vmask_rp]      @ bit select, deal with right pad\n"
                                "vmla.f32 q6, q15, %f[wr00][0]          @ mul weight0 02, out0r0\n"
                                "vmla.f32 q7, q15, %f[wr10][0]          @ mul weight1 02, out1r0\n"

                                //! 2nd row
                                "vext.32  q12, q10, q11, #1             @ shift left r1\n"
                                "vmla.f32 q6, q10, %e[wr01][0]          @ mul weight0 10, out0r0\n"
                                "vmla.f32 q7, q10, %e[wr11][0]          @ mul weight1 10, out1r0\n"

                                "vext.32  q15, q10, q11, #2             @ shift left r1\n"
                                "vmla.f32 q6, q12, %e[wr01][1]          @ mul weight0 11, out0r0\n"
                                "vmla.f32 q7, q12, %e[wr11][1]          @ mul weight1 11, out1r0\n"

                                "vld1.32  {d16-d17}, [%[doutc0r0]]      @ load dout0r0\n"
                                "vld1.32  {d18-d19}, [%[doutc1r0]]      @ load dout0r0\n"
                                "vmla.f32 q6, q15, %f[wr01][0]          @ mul weight0 12, out0r0\n"
                                "vmla.f32 q7, q15, %f[wr11][0]          @ mul weight1 12, out1r0\n"

                                "cmp %[relu], #1                        @ check whether has relu\n"
                                "blt    13f                             @ jump to store without relu\n"
                                "vmax.f32   q6, q6, %q[vzero]           @ relu\n"
                                "vmax.f32   q7, q7, %q[vzero]           @ relu\n"

                                "13:                                    @ store mid right result\n"
                                // store bl1 result

                                "vmvn.32  q12, %q[vzero]                      @ \n"
                                "vext.32  q15, q12, %q[vmask_rp], #3    @ shift mask right 1\n"
                                "vbif q6, q8, q15                       @ bit select\n"
                                "vbif q7, q9, q15                       @ bit select\n"

                                "vst1.32  {d12-d13}, [%[doutc0r0]]      @ store result, add pointer\n"
                                "vst1.32  {d14-d15}, [%[doutc1r0]]      @ store result, add pointer\n"
                        :[doutc0r0] "+r"(doutc0r0), [doutc1r0] "+r" (doutc1r0),\
                            [din0_ptr] "+r"(din0_ptr), [din1_ptr] "+r"(din1_ptr), \
                            [cnt] "+r"(cnt)
                        :[wr00] "w"(wr00), [wr01] "w"(wr01), \
                            [wr10] "w"(wr10), [wr11] "w"(wr11), \
                            [vmask_rp] "w" (vmask_rp), [vzero] "w" (vzero), \
                            [relu] "r" (relu)
                        :"q6", "q7", "q8", "q9", "q10", "q11", "q12", "q13", "q15"
                        );
#endif //__aarch64__

                    } else { // write 2 rows
                        din2_ptr = dr2;
#ifdef __aarch64__
                        // todo
#else
                        int cnt = cnt_col;
                        asm volatile (
                        //! process left pad
                        "pld [%[doutc0r0]]                     @ preload data\n"
                                "pld [%[din0_ptr]]                      @ preload data\n"
                                "pld [%[doutc1r0]]                      @ preload data\n"
                                "pld [%[doutc0r1]]                      @ preload data\n"
                                "vld1.32  {d26-d27}, [%[doutc0r0]]      @ load dout0r0\n"
                                "vld1.32  {d20-d22}, [%[din0_ptr]]!     @ load din r0\n"
                                "vld1.32  {d16-d17}, [%[doutc1r0]]      @ load dout1r0\n"
                                "pld [%[doutc1r1]]                      @ preload data\n"
                                "pld [%[din1_ptr]]                      @ preload data\n"
                                "pld [%[din2_ptr]]                      @ preload data\n"

                                //! 1st row
                                "vext.32  q12, q10, q11, #1             @ shift left r0\n"
                                "vmla.f32 q13, q10, %e[wr00][1]         @ mul weight0 01, out0r0\n"
                                "vmla.f32 q8, q10, %e[wr10][1]          @ mul weight1 01, out1r0\n"

                                //"pld [%[din0_ptr], #192]                @ preload data\n"
                                "vld1.32  {d28-d29}, [%[doutc0r1]]      @ load dout0r1\n"
                                "vld1.32  {d18-d19}, [%[doutc1r1]]      @ load dout1r1\n"
                                "sub %[din0_ptr], #12                   @ 1pad + 2 float data overlap\n"

                                "vext.32  q15, %q[vzero], q10, #3       @ shift right r1\n"
                                "vmla.f32 q13, q12, %f[wr00][0]         @ mul weight0 02, out0r0\n"
                                "vmla.f32 q8, q12, %f[wr10][0]          @ mul weight1 02, out1r0\n"

                                //  "vmov.u32 q15, #0                         @ dump zero\n"
                                "vld1.32  {d20-d22}, [%[din1_ptr]]!     @ load din r1\n"
                                "pld [%[din0_ptr]]                      @ preload data\n"
                                "vmla.f32 q13, q15, %e[wr00][0]         @ mul weight0 00, out0r0\n"
                                "vmla.f32 q8, q15, %e[wr10][0]          @ mul weight1 00, out1r0\n"

                                //! 2nd row
                                "sub %[din1_ptr], #12                   @ 1pad + 2 float data overlap\n"
                                "vext.32  q12, q10, q11, #1             @ shift left r1\n"
                                "vmla.f32 q14, q10, %e[wr00][1]         @ mul weight0 01, out0r1\n"
                                "vmla.f32 q9, q10, %e[wr10][1]          @ mul weight1 01, out1r1\n"
                                "vmla.f32 q13, q10, %e[wr01][1]         @ mul weight0 11, out0r0\n"
                                "vmla.f32 q8, q10, %e[wr11][1]          @ mul weight1 11, out1r0\n"

                                // "pld [%[din1_ptr], #192]                @ preload data\n"
                                "vext.32  q15, %q[vzero], q10, #3       @ shift right r1\n"
                                "vmla.f32 q14, q12, %f[wr00][0]         @ mul weight0 02, out0r1\n"
                                "vmla.f32 q9, q12, %f[wr10][0]          @ mul weight1 02, out1r1\n"
                                "vmla.f32 q13, q12, %f[wr01][0]         @ mul weight0 12, out0r0\n"
                                "vmla.f32 q8, q12, %f[wr11][0]          @ mul weight1 12, out1r0\n"

                                "vld1.32  {d20-d22}, [%[din2_ptr]]      @ load din r2\n"
                                "vmla.f32 q14, q15, %e[wr00][0]         @ mul weight0 00, out0r1\n"
                                "vmla.f32 q9, q15, %e[wr10][0]          @ mul weight1 00, out1r1\n"
                                "vmla.f32 q13, q15, %e[wr01][0]         @ mul weight0 10, out0r0\n"
                                "vmla.f32 q8, q15, %e[wr11][0]          @ mul weight1 10, out1r0\n"

                                //! 3rd row
                                "add %[din2_ptr], #12                   @ 1pad + 2 float data overlap\n"
                                "vext.32  q12, q10, q11, #1             @ shift left r2\n"
                                "vmla.f32 q14, q10, %e[wr01][1]         @ mul weight0 11, out0r1\n"
                                "vmla.f32 q9, q10, %e[wr11][1]          @ mul weight1 11, out1r1\n"
                                "vmla.f32 q13, q10, %e[wr02][1]         @ mul weight0 21, out0r0\n"
                                "vmla.f32 q8, q10, %e[wr12][1]          @ mul weight1 21, out1r0\n"

                                "vext.32  q15, %q[vzero], q10, #3       @ shift right r2\n"
                                "vmla.f32 q14, q12, %f[wr01][0]         @ mul weight0 12, out0r1\n"
                                "vmla.f32 q9, q12, %f[wr11][0]          @ mul weight1 12, out1r1\n"
                                "vmla.f32 q13, q12, %f[wr02][0]         @ mul weight0 22, out0r0\n"
                                "vmla.f32 q8, q12, %f[wr12][0]          @ mul weight1 22, out1r0\n"

                                "pld [%[din1_ptr]]                      @ preload data\n"
                                "vmla.f32 q14, q15, %e[wr01][0]         @ mul weight0 10, out0r1\n"
                                "vmla.f32 q9, q15, %e[wr11][0]          @ mul weight1 10, out1r1\n"
                                "vmla.f32 q13, q15, %e[wr02][0]         @ mul weight0 20, out0r0\n"
                                "vmla.f32 q8, q15, %e[wr12][0]          @ mul weight1 20, out1r0\n"
                                "pld [%[din2_ptr]]                      @ preload data\n"

                                "cmp %[relu], #1                        @ check whether has relu\n"
                                "blt    14f                             @ jump to store without relu\n"
                                "vmax.f32   q14, q14, %q[vzero]         @ relu\n"
                                "vmax.f32   q9, q9, %q[vzero]           @ relu\n"
                                "vmax.f32   q13, q13, %q[vzero]         @ relu\n"
                                "vmax.f32   q8, q8, %q[vzero]           @ relu\n"

                                "14:                                    @ store mid right result\n"
                                // store bl2 result
                                "vst1.32  {d28-d29}, [%[doutc0r1]]!     @ store result, add pointer\n"
                                "vst1.32  {d18-d19}, [%[doutc1r1]]!     @ store result, add pointer\n"
                                "vst1.32  {d26-d27}, [%[doutc0r0]]!     @ store result, add pointer\n"
                                "vst1.32  {d16-d17}, [%[doutc1r0]]!     @ store result, add pointer\n"

                                "pld [%[doutc0r1]]                      @ preload data\n"
                                "pld [%[doutc1r1]]                      @ preload data\n"
                                "pld [%[doutc0r0]]                      @ preload data\n"
                                "pld [%[doutc1r0]]                      @ preload data\n"

                                //"sub %[din0_ptr], #12                   @ 1pad + 2 float data overlap\n"
                                //"sub %[din1_ptr], #12                   @ 1pad + 2 float data overlap\n"
                                //"add %[din2_ptr], #12                   @ 1pad + 2 float data overlap\n"

                                //! process mid cols
                                "cmp %[cnt], #1                         @ check whether has mid cols\n"
                                "blt  15f                               @ jump to main loop start point\n"
                                "conv3x3_bot_mid_2:                         @ main loop start point\n"

                                "vld1.32  {d20-d22}, [%[din0_ptr]]!      @ load din r0\n"
                                "vld1.32  {d26-d27}, [%[doutc0r0]]       @ load dout0r0\n"
                                "vld1.32  {d16-d17}, [%[doutc1r0]]        @ load dout1r0\n"
                                "vld1.32  {d28-d29}, [%[doutc0r1]]       @ load dout0r1\n"
                                "vld1.32  {d18-d19}, [%[doutc1r1]]        @ load dout1r1\n"
                                "sub %[din0_ptr], #8                    @ 2 float data overlap with previous data\n"

                                //! 1st row
                                "vext.32  q12, q10, q11, #1                 @ shift left r0\n"
                                "vmla.f32 q13, q10, %e[wr00][0]           @ mul weight0 00, out0r0\n"
                                "vmla.f32 q8, q10, %e[wr10][0]           @ mul weight1 00, out1r0\n"

                                "pld [%[din0_ptr]]                        @ preload data\n"

                                "vext.32  q15, q10, q11, #2               @ shift left r0\n"
                                "vmla.f32 q13, q12, %e[wr00][1]              @ mul weight0 01, out0r0\n"
                                "vmla.f32 q8, q12, %e[wr10][1]              @ mul weight1 01, out1r0\n"

                                "vld1.32  {d20-d22}, [%[din1_ptr]]!       @ load din r1\n"
                                "vmla.f32 q13, q15, %f[wr00][0]            @ mul weight0 02, out0r0\n"
                                "vmla.f32 q8, q15, %f[wr10][0]            @ mul weight1 02, out1r0\n"

                                //! 2nd row
                                "sub %[din1_ptr], #8                    @ 2 float data overlap with previous data\n"
                                "vext.32  q12, q10, q11, #1               @ shift left r1\n"
                                "vmla.f32 q14, q10, %e[wr00][0]           @ mul weight0 00, out0r1\n"
                                "vmla.f32 q9, q10, %e[wr10][0]           @ mul weight1 00, out1r1\n"
                                "vmla.f32 q13, q10, %e[wr01][0]           @ mul weight0 10, out0r0\n"
                                "vmla.f32 q8, q10, %e[wr11][0]           @ mul weight1 10, out1r0\n"

                                // "pld [%[din1_ptr], #192]                @ preload data\n"

                                "vext.32  q15, q10, q11, #2               @ shift left r1\n"
                                "vmla.f32 q14, q12, %e[wr00][1]            @ mul weight0 01, out0r1\n"
                                "vmla.f32 q9, q12, %e[wr10][1]            @ mul weight1 01, out1r1\n"
                                "vmla.f32 q13, q12, %e[wr01][1]            @ mul weight0 11, out0r0\n"
                                "vmla.f32 q8, q12, %e[wr11][1]            @ mul weight1 11, out1r0\n"

                                "vld1.32  {d20-d22}, [%[din2_ptr]]       @ load din r2\n"
                                "vmla.f32 q14, q15, %f[wr00][0]            @ mul weight0 02, out0r1\n"
                                "vmla.f32 q9, q15, %f[wr10][0]            @ mul weight1 02, out1r1\n"
                                "vmla.f32 q13, q15, %f[wr01][0]            @ mul weight0 12, out0r0\n"
                                "vmla.f32 q8, q15, %f[wr11][0]            @ mul weight1 12, out1r0\n"

                                //! 3rd row
                                "add %[din2_ptr], #16                    @ 2 float data overlap with previous data\n"
                                "vext.32  q12, q10, q11, #1               @ shift left r2\n"
                                "vmla.f32 q14, q10, %e[wr01][0]           @ mul weight0 10, out0r1\n"
                                "vmla.f32 q9, q10, %e[wr11][0]           @ mul weight1 10, out1r1\n"
                                "vmla.f32 q13, q10, %e[wr02][0]           @ mul weight0 20, out0r0\n"
                                "vmla.f32 q8, q10, %e[wr12][0]           @ mul weight1 20, out1r0\n"

                                "vext.32  q15, q10, q11, #2               @ shift right r2\n"
                                "vmla.f32 q13, q12, %e[wr02][1]            @ mul weight0 21, out0r0\n"
                                "vmla.f32 q14, q12, %e[wr01][1]            @ mul weight0 11, out0r1\n"
                                "vmla.f32 q8, q12, %e[wr12][1]            @ mul weight1 21, out1r0\n"
                                "vmla.f32 q9, q12, %e[wr11][1]            @ mul weight1 11, out1r1\n"

                                "pld [%[din1_ptr]]                        @ preload data\n"
                                "vmla.f32 q14, q15, %f[wr01][0]            @ mul weight0 12, out0r1\n"
                                "vmla.f32 q9, q15, %f[wr11][0]            @ mul weight1 12, out1r1\n"
                                "vmla.f32 q13, q15, %f[wr02][0]            @ mul weight0 22, out0r0\n"
                                "vmla.f32 q8, q15, %f[wr12][0]            @ mul weight1 22, out1r0\n"
                                "pld [%[din2_ptr]]                        @ preload data\n"

                                "cmp %[relu], #1                        @ check whether has relu\n"
                                "blt    16f                             @ jump to store without relu\n"
                                "vmax.f32   q14, q14, %q[vzero]         @ relu\n"
                                "vmax.f32   q9, q9, %q[vzero]           @ relu\n"
                                "vmax.f32   q13, q13, %q[vzero]         @ relu\n"
                                "vmax.f32   q8, q8, %q[vzero]           @ relu\n"

                                "16:                                    @ store mid right result\n"
                                // store bl2 result

                                "vst1.32  {d28-d29}, [%[doutc0r1]]!     @ store result, add pointer\n"
                                "vst1.32  {d18-d19}, [%[doutc1r1]]!     @ store result, add pointer\n"
                                "vst1.32  {d26-d27}, [%[doutc0r0]]!     @ store result, add pointer\n"
                                "vst1.32  {d16-d17}, [%[doutc1r0]]!     @ store result, add pointer\n"

                                "pld [%[doutc0r1]]                      @ preload data\n"
                                "pld [%[doutc1r1]]                      @ preload data\n"
                                "pld [%[doutc0r0]]                      @ preload data\n"
                                "pld [%[doutc1r0]]                      @ preload data\n"

                                "subs %[cnt], #1                        @ loop count minus 1\n"
                                "bne    conv3x3_bot_mid_2                   @ jump to main loop start point\n"

                                //! process right pad
                                "15:                                    @ right pad entry\n"

                                "vld1.32  {d20-d22}, [%[din0_ptr]]!      @ load din r0\n"
                                "vld1.32  {d26-d27}, [%[doutc0r0]]       @ load dout0r0\n"
                                "vld1.32  {d16-d17}, [%[doutc1r0]]        @ load dout1r0\n"
                                "vld1.32  {d28-d29}, [%[doutc0r1]]       @ load dout0r1\n"
                                "vld1.32  {d18-d19}, [%[doutc1r1]]        @ load dout1r1\n"

                                //! 1st row
                                "vbif d21, %e[vzero], %e[vmask_rp]             @ bit select, deal with right pad\n"
                                "vbif d22, %e[vzero], %f[vmask_rp]             @ bit select, deal with right pad\n"
                                "vext.32  q12, q10, q11, #1                 @ shift left r0\n"
                                "vmla.f32 q13, q10, %e[wr00][0]           @ mul weight0 00, out0r0\n"
                                "vmla.f32 q8, q10, %e[wr10][0]           @ mul weight1 00, out1r0\n"

                                "vext.32  q15, q10, q11, #2               @ shift left r0\n"
                                "vld1.32  {d20-d22}, [%[din1_ptr]]!       @ load din r1\n"
                                "vmla.f32 q13, q12, %e[wr00][1]              @ mul weight0 01, out0r0\n"
                                "vmla.f32 q8, q12, %e[wr10][1]              @ mul weight1 01, out1r0\n"

                                "vmla.f32 q13, q15, %f[wr00][0]            @ mul weight0 02, out0r0\n"
                                "vmla.f32 q8, q15, %f[wr10][0]            @ mul weight1 02, out1r0\n"

                                //! 2nd row
                                "vbif d21, d31, %e[vmask_rp]             @ bit select, deal with right pad\n"
                                "vbif d22, d31, %f[vmask_rp]             @ bit select, deal with right pad\n"
                                "vext.32  q12, q10, q11, #1               @ shift left r1\n"
                                "vmla.f32 q13, q10, %e[wr01][0]           @ mul weight0 10, out0r0\n"
                                "vmla.f32 q14, q10, %e[wr00][0]           @ mul weight0 00, out0r1\n"
                                "vmla.f32 q8, q10, %e[wr11][0]           @ mul weight1 10, out1r0\n"
                                "vmla.f32 q9, q10, %e[wr10][0]           @ mul weight1 00, out1r1\n"

                                "vext.32  q15, q10, q11, #2               @ shift left r1\n"
                                "vmla.f32 q13, q12, %e[wr01][1]            @ mul weight0 11, out0r0\n"
                                "vmla.f32 q14, q12, %e[wr00][1]            @ mul weight0 01, out0r1\n"
                                "vmla.f32 q8, q12, %e[wr11][1]            @ mul weight1 11, out1r0\n"
                                "vmla.f32 q9, q12, %e[wr10][1]            @ mul weight1 01, out1r1\n"

                                "vld1.32  {d20-d22}, [%[din2_ptr]]       @ load din r2\n"
                                "vmla.f32 q13, q15, %f[wr01][0]            @ mul weight0 12, out0r0\n"
                                "vmla.f32 q14, q15, %f[wr00][0]            @ mul weight0 02, out0r1\n"
                                "vmla.f32 q8, q15, %f[wr11][0]            @ mul weight1 12, out1r0\n"
                                "vmla.f32 q9, q15, %f[wr10][0]            @ mul weight1 02, out1r1\n"

                                //! 3rd row
                                "vbif d21, d31, %e[vmask_rp]             @ bit select, deal with right pad\n"
                                "vbif d22, d31, %f[vmask_rp]             @ bit select, deal with right pad\n"
                                "vext.32  q12, q10, q11, #1               @ shift left r2\n"
                                "vmla.f32 q13, q10, %e[wr02][0]           @ mul weight0 20, out0r0\n"
                                "vmla.f32 q14, q10, %e[wr01][0]           @ mul weight0 10, out0r1\n"
                                "vmla.f32 q8, q10, %e[wr12][0]           @ mul weight1 20, out1r0\n"
                                "vmla.f32 q9, q10, %e[wr11][0]           @ mul weight1 10, out1r1\n"

                                "vext.32  q15, q10, q11, #2               @ shift right r2\n"
                                "vmla.f32 q13, q12, %e[wr02][1]            @ mul weight0 21, out0r0\n"
                                "vmla.f32 q14, q12, %e[wr01][1]            @ mul weight0 11, out0r1\n"
                                "vmla.f32 q8, q12, %e[wr12][1]            @ mul weight1 21, out1r0\n"
                                "vmla.f32 q9, q12, %e[wr11][1]            @ mul weight1 11, out1r1\n"

                                "vld1.32  {d20-d21}, [%[doutc0r0]]       @ load dout0r0\n"
                                "vld1.32  {d22-d23}, [%[doutc1r0]]       @ load dout0r1\n"
                                "vmla.f32 q13, q15, %f[wr02][0]            @ mul weight0 22, out0r0\n"
                                "vmla.f32 q14, q15, %f[wr01][0]            @ mul weight0 12, out0r1\n"
                                "vmla.f32 q8, q15, %f[wr12][0]            @ mul weight1 22, out1r0\n"
                                "vmla.f32 q9, q15, %f[wr11][0]            @ mul weight1 12, out1r1\n"

                                "cmp %[relu], #1                        @ check whether has relu\n"
                                "blt    17f                             @ jump to store without relu\n"
                                "vmax.f32   q14, q14, %q[vzero]         @ relu\n"
                                "vmax.f32   q9, q9, %q[vzero]           @ relu\n"
                                "vmax.f32   q13, q13, %q[vzero]         @ relu\n"
                                "vmax.f32   q8, q8, %q[vzero]           @ relu\n"

                                "17:                                    @ store mid right result\n"
                                // store br2 result

                                "vmvn.32  q12, %q[vzero]                @ \n"
                                "vext.32  q15, q12, %q[vmask_rp], #3    @ shift mask right 1\n"
                                "vbif q13, q10, q15                     @ bit select\n"
                                "vbif q8, q11, q15                      @ bit select\n"

                                "vld1.32  {d20-d21}, [%[doutc0r1]]       @ load dout0r0\n"
                                "vld1.32  {d22-d23}, [%[doutc1r1]]       @ load dout0r1\n"

                                "vst1.32  {d26-d27}, [%[doutc0r0]]      @ store result, add pointer\n"
                                "vst1.32  {d16-d17}, [%[doutc1r0]]      @ store result, add pointer\n"

                                "vbif q14, q10, q15                     @ bit select\n"
                                "vbif q9, q11, q15                      @ bit select\n"

                                "vst1.32  {d28-d29}, [%[doutc0r1]]      @ store result, add pointer\n"
                                "vst1.32  {d18-d19}, [%[doutc1r1]]      @ store result, add pointer\n"

                        :[doutc0r0] "+r"(doutc0r0), [doutc0r1] "+r"(doutc0r1), \
                            [doutc1r0] "+r" (doutc1r0), [doutc1r1] "+r" (doutc1r1),\
                            [din0_ptr] "+r"(din0_ptr), [din1_ptr] "+r"(din1_ptr), \
                            [din2_ptr] "+r"(din2_ptr), [cnt] "+r"(cnt)
                        :[wr00] "w"(wr00), [wr01] "w"(wr01), [wr02] "w"(wr02), \
                            [wr10] "w"(wr10), [wr11] "w"(wr11), [wr12] "w"(wr12), \
                            [vmask_rp] "w" (vmask_rp), [vzero] "w" (vzero), [relu] "r" (relu)
                        :"q8", "q9", "q10", \
                            "q11", "q12", "q13", "q14", "q15"
                        );
#endif //__aarch64__
                    }
                } // end of processing bottom pad
            } // end of processing channels
        } //end of processing output channel
        if (cremain > 0) {
            for (int c = 0; c < cremain; ++c) {

                int cidx = ch_out - cremain + c;
                float* dout_c = dout_batch + cidx * size_out_channel;

                if (flag_bias) {
                    fill_bias(dout_c, &bias[cidx], 1, size_out_channel);
                } else {
                    fill_bias(dout_c, zero, 1, size_out_channel);
                }

                const float* wc0 = weights + cidx * w_stride;

                for (int i = 0; i < ch_in; ++i) {

                    bool relu = (i == ch_in - 1) && flag_relu;

                    const float* din_channel = din_batch + i * size_in_channel;
                    for (int h = 0; h < h_out; ++h) {

                        int hstart = h - pad_h;
                        int hend = hstart + 3;
                        hstart = std::max(hstart, 0);
                        hend = std::min(hend, h_in);

                        int khstart = hend < kernel_h? kernel_h - hend : 0;

                        float* dout_row = dout_c + h * w_out;

                        for (int w = 0; w < w_out; ++w) {
                            int wstart = w - pad_w;
                            int wend = wstart + 3;
                            wstart = std::max(wstart, 0);
                            wend = std::min(wend, w_in);
                            int kwstart = wend < kernel_w? kernel_w - wend : 0;

                            for (int kh = hstart; kh < hend; ++kh) {
                                for (int kw = wstart; kw < wend; ++kw) {
                                    dout_row[w] += din_channel[kh * w_in + kw] * \
                                        wc0[(khstart + kh - hstart) * 3 + kwstart + kw - wstart];
                                }
                            }
                            if (relu) {
                                dout_row[w] = dout_row[w] > 0.f? dout_row[w] : 0.f;
                            }
                        }
                    }
                    wc0 += 9;
                }
            }
        } // end of remain out channel

    } // end of processing batchs
}

} //namespace lite

} //namespace saber

} //namespace anakin

#endif
