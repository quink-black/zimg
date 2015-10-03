#ifdef ZIMG_X86

#include <algorithm>
#include <xmmintrin.h>
#include "common/align.h"
#include "common/osdep.h"
#include "common/pixel.h"
#include "common/linebuffer.h"
#include "graph/zfilter.h"
#include "filter.h"
#include "resize_impl.h"
#include "resize_impl_x86.h"

namespace zimg {;
namespace resize {;

namespace {;

inline FORCE_INLINE void scatter4_ps(float *dst0, float *dst1, float *dst2, float *dst3, __m128 x)
{
	_mm_store_ss(dst0, x);
	x = _mm_shuffle_ps(x, x, _MM_SHUFFLE(3, 2, 1, 1));
	_mm_store_ss(dst1, x);
	x = _mm_shuffle_ps(x, x, _MM_SHUFFLE(3, 2, 1, 2));
	_mm_store_ss(dst2, x);
	x = _mm_shuffle_ps(x, x, _MM_SHUFFLE(3, 2, 1, 3));
	_mm_store_ss(dst3, x);
}

inline FORCE_INLINE void mm_store_left(float *dst, __m128 x, unsigned count)
{
	switch (count - 1) {
	case 2:
		x = _mm_shuffle_ps(x, x, _MM_SHUFFLE(3, 2, 1, 1));
		_mm_store_ss(dst + 1, x);
	case 1:
		x = _mm_shuffle_ps(x, x, _MM_SHUFFLE(3, 2, 1, 2));
		_mm_store_ss(dst + 2, x);
	case 0:
		x = _mm_shuffle_ps(x, x, _MM_SHUFFLE(3, 2, 1, 3));
		_mm_store_ss(dst + 3, x);
	}
}

inline FORCE_INLINE void mm_store_right(float *dst, __m128 x, unsigned count)
{
	__m128 y;

	switch (count - 1) {
	case 2:
		y = _mm_shuffle_ps(x, x, _MM_SHUFFLE(3, 2, 1, 2));
		_mm_store_ss(dst + 2, y);
	case 1:
		y = _mm_shuffle_ps(x, x, _MM_SHUFFLE(3, 2, 1, 1));
		_mm_store_ss(dst + 1, y);
	case 0:
		y = _mm_shuffle_ps(x, x, _MM_SHUFFLE(3, 2, 1, 0));
		_mm_store_ss(dst + 0, y);
	}
}

void transpose_line_4x4_ps(float *dst, const float *src_p0, const float *src_p1, const float *src_p2, const float *src_p3, unsigned left, unsigned right)
{
	for (unsigned j = left; j < right; j += 4) {
		__m128 x0, x1, x2, x3;

		x0 = _mm_load_ps(src_p0 + j);
		x1 = _mm_load_ps(src_p1 + j);
		x2 = _mm_load_ps(src_p2 + j);
		x3 = _mm_load_ps(src_p3 + j);

		_MM_TRANSPOSE4_PS(x0, x1, x2, x3);

		_mm_store_ps(dst + 0, x0);
		_mm_store_ps(dst + 4, x1);
		_mm_store_ps(dst + 8, x2);
		_mm_store_ps(dst + 12, x3);

		dst += 16;
	}
}


template <unsigned FWidth, unsigned Tail>
inline FORCE_INLINE __m128 resize_line4_h_f32_sse_xiter(unsigned j,
                                                        const unsigned *filter_left, const float * RESTRICT filter_data, unsigned filter_stride, unsigned filter_width,
                                                        const float * RESTRICT src_ptr, unsigned src_base)
{
	const float *filter_coeffs = filter_data + j * filter_stride;
	const float *src_p = src_ptr + (filter_left[j] - src_base) * 4;

	__m128 accum0 = _mm_setzero_ps();
	__m128 accum1 = _mm_setzero_ps();
	__m128 x, c, coeffs;

	unsigned k_end = FWidth ? FWidth - Tail : mod(filter_width, 4);

	for (unsigned k = 0; k < k_end; k += 4) {
		coeffs = _mm_load_ps(filter_coeffs + k);

		c = _mm_shuffle_ps(coeffs, coeffs, _MM_SHUFFLE(0, 0, 0, 0));
		x = _mm_load_ps(src_p + (k + 0) * 4);
		x = _mm_mul_ps(c, x);
		accum0 = _mm_add_ps(accum0, x);

		c = _mm_shuffle_ps(coeffs, coeffs, _MM_SHUFFLE(1, 1, 1, 1));
		x = _mm_load_ps(src_p + (k + 1) * 4);
		x = _mm_mul_ps(c, x);
		accum1 = _mm_add_ps(accum1, x);

		c = _mm_shuffle_ps(coeffs, coeffs, _MM_SHUFFLE(2, 2, 2, 2));
		x = _mm_load_ps(src_p + (k + 2) * 4);
		x = _mm_mul_ps(c, x);
		accum0 = _mm_add_ps(accum0, x);

		c = _mm_shuffle_ps(coeffs, coeffs, _MM_SHUFFLE(3, 3, 3, 3));
		x = _mm_load_ps(src_p + (k + 3) * 4);
		x = _mm_mul_ps(c, x);
		accum1 = _mm_add_ps(accum1, x);
	}

	if (Tail >= 1) {
		coeffs = _mm_load_ps(filter_coeffs + k_end);

		c = _mm_shuffle_ps(coeffs, coeffs, _MM_SHUFFLE(0, 0, 0, 0));
		x = _mm_load_ps(src_p + (k_end + 0) * 4);
		x = _mm_mul_ps(c, x);
		accum0 = _mm_add_ps(accum0, x);
	}
	if (Tail >= 2) {
		c = _mm_shuffle_ps(coeffs, coeffs, _MM_SHUFFLE(1, 1, 1, 1));
		x = _mm_load_ps(src_p + (k_end + 1) * 4);
		x = _mm_mul_ps(c, x);
		accum1 = _mm_add_ps(accum1, x);
	}
	if (Tail >= 3) {
		c = _mm_shuffle_ps(coeffs, coeffs, _MM_SHUFFLE(2, 2, 2, 2));
		x = _mm_load_ps(src_p + (k_end + 2) * 4);
		x = _mm_mul_ps(c, x);
		accum0 = _mm_add_ps(accum0, x);
	}
	if (Tail >= 4) {
		c = _mm_shuffle_ps(coeffs, coeffs, _MM_SHUFFLE(3, 3, 3, 3));
		x = _mm_load_ps(src_p + (k_end + 3) * 4);
		x = _mm_mul_ps(c, x);
		accum1 = _mm_add_ps(accum1, x);
	}

	if (!FWidth || FWidth >= 2)
		accum0 = _mm_add_ps(accum0, accum1);

	return accum0;
}

template <unsigned FWidth, unsigned Tail>
void resize_line4_h_f32_sse(const unsigned *filter_left, const float * RESTRICT filter_data, unsigned filter_stride, unsigned filter_width,
                            const float * RESTRICT src_ptr, float * const *dst_ptr, unsigned left, unsigned right)
{
	unsigned src_base = mod(filter_left[left], 4);

	unsigned vec_begin = align(left, 4);
	unsigned vec_end = mod(right, 4);

	float * RESTRICT dst_p0 = dst_ptr[0];
	float * RESTRICT dst_p1 = dst_ptr[1];
	float * RESTRICT dst_p2 = dst_ptr[2];
	float * RESTRICT dst_p3 = dst_ptr[3];
#define XITER resize_line4_h_f32_sse_xiter<FWidth, Tail>
#define XARGS filter_left, filter_data, filter_stride, filter_width, src_ptr, src_base
	for (unsigned j = left; j < vec_begin; ++j) {
		__m128 x = XITER(j, XARGS);
		scatter4_ps(dst_p0 + j, dst_p1 + j, dst_p2 + j, dst_p3 + j, x);
	}

	for (unsigned j = vec_begin; j < vec_end; j += 4) {
		__m128 x0, x1, x2, x3;

		x0 = XITER(j + 0, XARGS);
		x1 = XITER(j + 1, XARGS);
		x2 = XITER(j + 2, XARGS);
		x3 = XITER(j + 3, XARGS);

		_MM_TRANSPOSE4_PS(x0, x1, x2, x3);

		_mm_store_ps(dst_p0 + j, x0);
		_mm_store_ps(dst_p1 + j, x1);
		_mm_store_ps(dst_p2 + j, x2);
		_mm_store_ps(dst_p3 + j, x3);
	}

	for (unsigned j = vec_end; j < right; ++j) {
		__m128 x = XITER(j, XARGS);
		scatter4_ps(dst_p0 + j, dst_p1 + j, dst_p2 + j, dst_p3 + j, x);
	}
#undef XITER
#undef XARGS
}

const decltype(&resize_line4_h_f32_sse<0, 0>) resize_line4_h_f32_sse_jt_small[] = {
	resize_line4_h_f32_sse<1, 1>,
	resize_line4_h_f32_sse<2, 2>,
	resize_line4_h_f32_sse<3, 3>,
	resize_line4_h_f32_sse<4, 4>,
	resize_line4_h_f32_sse<5, 1>,
	resize_line4_h_f32_sse<6, 2>,
	resize_line4_h_f32_sse<7, 3>,
	resize_line4_h_f32_sse<8, 4>
};

const decltype(&resize_line4_h_f32_sse<0, 0>) resize_line4_h_f32_sse_jt_large[] = {
	resize_line4_h_f32_sse<0, 0>,
	resize_line4_h_f32_sse<0, 1>,
	resize_line4_h_f32_sse<0, 2>,
	resize_line4_h_f32_sse<0, 3>
};


template <unsigned N, bool UpdateAccum>
inline FORCE_INLINE __m128 resize_line_v_f32_sse_xiter(unsigned j,
                                                       const float * RESTRICT src_p0, const float * RESTRICT src_p1,
                                                       const float * RESTRICT src_p2, const float * RESTRICT src_p3, const float * RESTRICT dst_p,
                                                       const __m128 &c0, const __m128 &c1, const __m128 &c2, const __m128 &c3)
{
	__m128 accum0 = _mm_setzero_ps();
	__m128 accum1 = _mm_setzero_ps();
	__m128 x;

	if (N >= 0) {
		x = _mm_load_ps(src_p0 + j);
		x = _mm_mul_ps(c0, x);
		accum0 = UpdateAccum ? _mm_add_ps(_mm_load_ps(dst_p + j), x) : x;
	}
	if (N >= 1) {
		x = _mm_load_ps(src_p1 + j);
		x = _mm_mul_ps(c1, x);
		accum1 = x;
	}
	if (N >= 2) {
		x = _mm_load_ps(src_p2 + j);
		x = _mm_mul_ps(c2, x);
		accum0 = _mm_add_ps(accum0, x);
	}
	if (N >= 3) {
		x = _mm_load_ps(src_p3 + j);
		x = _mm_mul_ps(c3, x);
		accum1 = _mm_add_ps(accum1, x);
	}

	accum0 = (N >= 1) ? _mm_add_ps(accum0, accum1) : accum0;
	return accum0;
}

template <unsigned N, bool UpdateAccum>
void resize_line_v_f32_sse(const float *filter_data, const float * const *src_lines, float *dst, unsigned left, unsigned right)
{
	const float * RESTRICT src_p0 = src_lines[0];
	const float * RESTRICT src_p1 = src_lines[1];
	const float * RESTRICT src_p2 = src_lines[2];
	const float * RESTRICT src_p3 = src_lines[3];
	float * RESTRICT dst_p = dst;

	unsigned vec_begin = align(left, 4);
	unsigned vec_end = mod(right, 4);

	const __m128 c0 = _mm_set_ps1(filter_data[0]);
	const __m128 c1 = _mm_set_ps1(filter_data[1]);
	const __m128 c2 = _mm_set_ps1(filter_data[2]);
	const __m128 c3 = _mm_set_ps1(filter_data[3]);

	__m128 accum;

#define XITER resize_line_v_f32_sse_xiter<N, UpdateAccum>
#define XARGS src_p0, src_p1, src_p2, src_p3, dst_p, c0, c1, c2, c3
	if (left != vec_begin) {
		accum = XITER(vec_begin - 4, XARGS);
		mm_store_left(dst_p + vec_begin - 4, accum, vec_begin - left);
	}

	for (unsigned j = vec_begin; j < vec_end; j += 4) {
		accum = XITER(j, XARGS);
		_mm_store_ps(dst_p + j, accum);
	}

	if (right != vec_end) {
		accum = XITER(vec_end, XARGS);
		mm_store_right(dst_p + vec_end, accum, right - vec_end);
	}
#undef XITER
#undef XARGS
}

const decltype(&resize_line_v_f32_sse<0, false>) resize_line_v_f32_sse_jt_a[] = {
	resize_line_v_f32_sse<0, false>,
	resize_line_v_f32_sse<1, false>,
	resize_line_v_f32_sse<2, false>,
	resize_line_v_f32_sse<3, false>,
};

const decltype(&resize_line_v_f32_sse<0, false>) resize_line_v_f32_sse_jt_b[] = {
	resize_line_v_f32_sse<0, true>,
	resize_line_v_f32_sse<1, true>,
	resize_line_v_f32_sse<2, true>,
	resize_line_v_f32_sse<3, true>,
};


class ResizeImplH_F32_SSE final : public ResizeImplH {
	decltype(&resize_line4_h_f32_sse<0, 0>) m_func;
public:
	ResizeImplH_F32_SSE(const FilterContext &filter, unsigned height) :
		ResizeImplH(filter, image_attributes{ filter.filter_rows, height, zimg::PixelType::FLOAT }),
		m_func{}
	{
		if (filter.filter_width <= 8)
			m_func = resize_line4_h_f32_sse_jt_small[filter.filter_width - 1];
		else
			m_func = resize_line4_h_f32_sse_jt_large[filter.filter_width % 4];
	}

	unsigned get_simultaneous_lines() const override
	{
		return 4;
	}

	size_t get_tmp_size(unsigned left, unsigned right) const override
	{
		auto range = get_required_col_range(left, right);
		return 4 * ((range.second - mod(range.first, 4) + 4) * sizeof(float));
	}

	void process(void *, const ZimgImageBufferConst &src, const ZimgImageBuffer &dst, void *tmp, unsigned i, unsigned left, unsigned right) const override
	{
		LineBuffer<const float> src_buf{ src };
		LineBuffer<float> dst_buf{ dst };
		auto range = get_required_col_range(left, right);

		const float *src_ptr[4] = { 0 };
		float *dst_ptr[4] = { 0 };
		float *transpose_buf = reinterpret_cast<float *>(tmp);
		unsigned height = get_image_attributes().height;

		src_ptr[0] = src_buf[std::min(i + 0, height - 1)];
		src_ptr[1] = src_buf[std::min(i + 1, height - 1)];
		src_ptr[2] = src_buf[std::min(i + 2, height - 1)];
		src_ptr[3] = src_buf[std::min(i + 3, height - 1)];

		transpose_line_4x4_ps(transpose_buf, src_ptr[0], src_ptr[1], src_ptr[2], src_ptr[3], mod(range.first, 4), align(range.second, 4));

		dst_ptr[0] = dst_buf[std::min(i + 0, height - 1)];
		dst_ptr[1] = dst_buf[std::min(i + 1, height - 1)];
		dst_ptr[2] = dst_buf[std::min(i + 2, height - 1)];
		dst_ptr[3] = dst_buf[std::min(i + 3, height - 1)];

		m_func(m_filter.left.data(), m_filter.data.data(), m_filter.stride, m_filter.filter_width,
		       transpose_buf, dst_ptr, left, right);
	}
};


class ResizeImplV_F32_SSE final : public ResizeImplV {
public:
	ResizeImplV_F32_SSE(const FilterContext &filter, unsigned width) :
		ResizeImplV(filter, image_attributes{ width, filter.filter_rows, zimg::PixelType::FLOAT })
	{
	}

	void process(void *, const ZimgImageBufferConst &src, const ZimgImageBuffer &dst, void *, unsigned i, unsigned left, unsigned right) const override
	{
		LineBuffer<const float> src_buf{ src };
		LineBuffer<float> dst_buf{ dst };

		const float *filter_data = m_filter.data.data() + i * m_filter.stride;
		unsigned filter_width = m_filter.filter_width;
		unsigned src_height = m_filter.input_width;

		const float *src_lines[4] = { 0 };
		float *dst_line = dst_buf[i];

		for (unsigned k = 0; k < filter_width; k += 4) {
			unsigned taps_remain = std::min(filter_width - k, 4U);
			unsigned top = m_filter.left[i] + k;

			src_lines[0] = src_buf[std::min(top + 0, src_height - 1)];
			src_lines[1] = src_buf[std::min(top + 1, src_height - 1)];
			src_lines[2] = src_buf[std::min(top + 2, src_height - 1)];
			src_lines[3] = src_buf[std::min(top + 3, src_height - 1)];

			if (k == 0)
				resize_line_v_f32_sse_jt_a[taps_remain - 1](filter_data + k, src_lines, dst_line, left, right);
			else
				resize_line_v_f32_sse_jt_b[taps_remain - 1](filter_data + k, src_lines, dst_line, left, right);
		}
	}
};

} // namespace


IZimgFilter *create_resize_impl_h_sse(const FilterContext &context, unsigned height, PixelType type, unsigned depth)
{
	IZimgFilter *ret = nullptr;

	if (type == zimg::PixelType::FLOAT)
		ret = new ResizeImplH_F32_SSE{ context, height };

	return ret;
}

IZimgFilter *create_resize_impl_v_sse(const FilterContext &context, unsigned width, PixelType type, unsigned depth)
{
	IZimgFilter *ret = nullptr;

	if (type == zimg::PixelType::FLOAT)
		ret = new ResizeImplV_F32_SSE{ context, width };

	return ret;
}

} // namespace resize
} // namespace zimg

#endif // ZIMG_X86