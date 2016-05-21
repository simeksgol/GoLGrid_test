// The words of the first column in grid go from grid [0] to grid [grid_rect.height - 1], and those of the next column are from grid [col_offset] to grid [col_offset + grid_rect.height - 1].
// The number of words corresponding to PREFERRED_VECTOR_BYTE_SIZE before and after each column are allocated and readable, to help implement egde conditions efficiently, and they should always be empty.
// The addresses of grid [0] and grid [col_offset] are aligned to MAX_SUPPORTED_VECTOR_BYTE_SIZE
// In some situations we distinguish between physical coordinates, where the top-left corner of the grid is (0, 0), and normal coordinates used by GoLGrid_set_cell etc. where the top-left corner can be any value

typedef struct
{
	Rect grid_rect;
	void *grid_alloc;
	u64 *grid;
	u64 col_offset;
	s32 pop_x_on;
	s32 pop_x_off;
	s32 pop_y_on;
	s32 pop_y_off;
	s64 generation;
} GoLGrid;

#define GOLGRID_WIDTH_GRANULARITY 64
#define GOLGRID_HEIGHT_GRANULARITY 8


// Internal functions

static __inline_at_will void GoLGrid_int_preinit (GoLGrid *gg)
{
	if (!gg)
		return (void) ffsc (__func__);
	
	Rect_make (&gg->grid_rect, 0, 0, 0, 0);
	gg->grid = NULL;
	gg->grid_alloc = NULL;
	gg->col_offset = 0;
	gg->pop_x_on = 0;
	gg->pop_x_off = 0;
	gg->pop_y_on = 0;
	gg->pop_y_off = 0;
	gg->generation = 0;
}

static __force_inline void GoLGrid_int_set_empty_population_rect (GoLGrid *gg)
{
	if (!gg)
		return (void) ffsc (__func__);
	
	gg->pop_x_on = gg->grid_rect.width >> 1;
	gg->pop_x_off = gg->grid_rect.width >> 1;
	gg->pop_y_on = gg->grid_rect.height >> 1;
	gg->pop_y_off = gg->grid_rect.height >> 1;
}

static __force_inline void GoLGrid_int_adjust_pop_rect_new_on_cell (GoLGrid *gg, s32 x, s32 y)
{
	// Caller is responsible for sanity check
	
	if (gg->pop_x_off <= gg->pop_x_on)
	{
		gg->pop_x_on = x;
		gg->pop_x_off = x + 1;
		gg->pop_y_on = y;
		gg->pop_y_off = y + 1;
	}
	else
	{
		if (gg->pop_x_on > x)
			gg->pop_x_on = x;
		else if (gg->pop_x_off < x + 1)
			gg->pop_x_off = x + 1;
		
		if (gg->pop_y_on > y)
			gg->pop_y_on = y;
		else if (gg->pop_y_off < y + 1)
			gg->pop_y_off = y + 1;
	}
}

// These four function assumes that even if pop_x_on, pop_x_off, pop_y_on and pop_y_off may not be fully accurate, any on-cells in the grid are at least within those bounds
// Only the first one, GoLGrid_int_tighten_pop_x_on, may be called if the grid could be entirely empty, and in that case it returns FALSE

static __force_inline int GoLGrid_int_tighten_pop_x_on (GoLGrid *gg)
{
	// Caller is responsible for sanity check
	
	s32 col_on = gg->pop_x_on >> 6;
	s32 col_off = (gg->pop_x_off + 63) >> 6;
	
	s32 row_on = align_down_s32 (gg->pop_y_on, PREFERRED_VECTOR_BYTE_SIZE / sizeof (u64));
	s32 row_off = align_up_s32 (gg->pop_y_off, PREFERRED_VECTOR_BYTE_SIZE / sizeof (u64));
	s32 row_cnt = row_off - row_on;
	
	u64 col_offset = align_down_u64 (gg->col_offset, PREFERRED_VECTOR_BYTE_SIZE / sizeof (u64));
	const u64 *entry = align_down_const_pointer (gg->grid + (col_offset * (u64) col_on) + (u64) row_on, PREFERRED_VECTOR_BYTE_SIZE);
	
	s32 col_ix;
	s32 row_ix;
	u64 or_of_column;
	
	for (col_ix = col_on; col_ix < col_off; col_ix++)
	{
		or_of_column = 0;
		for (row_ix = 0; row_ix < row_cnt; row_ix++)
			or_of_column |= entry [row_ix];
		
		if (or_of_column != 0)
			break;
		
		entry += col_offset;
	}
	
	if (or_of_column == 0)
	{
		GoLGrid_int_set_empty_population_rect (gg);
		return FALSE;
	}
	
	gg->pop_x_on = (64 * col_ix) + (63 - most_significant_bit_u64 (or_of_column));
	return TRUE;
}

static __force_inline void GoLGrid_int_tighten_pop_x_off (GoLGrid *gg)
{
	// Caller is responsible for sanity check
	
	s32 col_on = gg->pop_x_on >> 6;
	s32 col_off = (gg->pop_x_off + 63) >> 6;
	
	s32 row_on = align_down_s32 (gg->pop_y_on, PREFERRED_VECTOR_BYTE_SIZE / sizeof (u64));
	s32 row_off = align_up_s32 (gg->pop_y_off, PREFERRED_VECTOR_BYTE_SIZE / sizeof (u64));
	s32 row_cnt = row_off - row_on;
	
	u64 col_offset = align_down_u64 (gg->col_offset, PREFERRED_VECTOR_BYTE_SIZE / sizeof (u64));
	const u64 *entry = align_down_const_pointer (gg->grid + (col_offset * (u64) (col_off - 1)) + (u64) row_on, PREFERRED_VECTOR_BYTE_SIZE);
	
	s32 col_ix;
	s32 row_ix;
	u64 or_of_column;
	
	for (col_ix = col_off - 1; col_ix >= col_on; col_ix--)
	{
		or_of_column = 0;
		for (row_ix = 0; row_ix < row_cnt; row_ix++)
			or_of_column |= entry [row_ix];
		
		if (or_of_column != 0)
			break;
		
		entry -= col_offset;
	}
	
	gg->pop_x_off = (64 * col_ix) + (64 - least_significant_bit_u64 (or_of_column));
}

static __force_inline void GoLGrid_int_tighten_pop_y_on (GoLGrid *gg)
{
	// Caller is responsible for sanity check
	
	s32 col_on = gg->pop_x_on >> 6;
	s32 col_off = (gg->pop_x_off + 63) >> 6;
	s32 new_y_on = gg->pop_y_on;
	
	while (TRUE)
	{
		s32 col_ix;
		for (col_ix = col_on; col_ix < col_off; col_ix++)
			if (gg->grid [(gg->col_offset * (u64) col_ix) + (u64) new_y_on] != 0)
			{
				gg->pop_y_on = new_y_on;
				return;
			}
		
		new_y_on++;
	}
}

static __force_inline void GoLGrid_int_tighten_pop_y_off (GoLGrid *gg)
{
	// Caller is responsible for sanity check
	
	s32 col_on = gg->pop_x_on >> 6;
	s32 col_off = (gg->pop_x_off + 63) >> 6;
	s32 new_y_off = gg->pop_y_off;
	
	while (TRUE)
	{
		s32 col_ix;
		for (col_ix = col_on; col_ix < col_off; col_ix++)
			if (gg->grid [(gg->col_offset * (u64) col_ix) + (u64) new_y_off - 1] != 0)
			{
				gg->pop_y_off = new_y_off;
				return;
			}
		
		new_y_off--;
	}
}

static __force_inline void GoLGrid_int_adjust_pop_rect_new_off_cell (GoLGrid *gg, s32 x, s32 y)
{
	// Caller is responsible for sanity check
	
	if (x == gg->pop_x_on)
		if (!GoLGrid_int_tighten_pop_x_on (gg))
			return;
	
	if (x == gg->pop_x_off - 1)
		GoLGrid_int_tighten_pop_x_off (gg);
	if (y == gg->pop_y_on)
		GoLGrid_int_tighten_pop_y_on (gg);
	if (y == gg->pop_y_off - 1)
		GoLGrid_int_tighten_pop_y_off (gg);
}

// This assumes the specified box exactly describes the population bounding box of the ored pattern
static __force_inline void GoLGrid_int_adjust_pop_rect_ored_bounding_box (GoLGrid *gg, s32 ored_x_on, s32 ored_x_off, s32 ored_y_on, s32 ored_y_off)
{
	if (!gg || !gg->grid)
		return (void) ffsc (__func__);
	
	if (ored_x_off <= ored_x_on || ored_y_off <= ored_y_on)
		return;
	
	if (gg->pop_x_off <= gg->pop_x_on)
	{
		gg->pop_x_on = ored_x_on;
		gg->pop_x_off = ored_x_off;
		gg->pop_y_on = ored_y_on;
		gg->pop_y_off = ored_y_off;
	}
	else
	{
		gg->pop_x_on = lowest_of_s32 (gg->pop_x_on, ored_x_on);
		gg->pop_x_off = highest_of_s32 (gg->pop_x_off, ored_x_off);
		gg->pop_y_on = lowest_of_s32 (gg->pop_y_on, ored_y_on);
		gg->pop_y_off = highest_of_s32 (gg->pop_y_off, ored_y_off);
	}
}

// We expect this function to be vectorized, so row_on and row_cnt should be aligned according to the expected vector size. Inlining is expected to propagate the alignment information from the calling
// function, to prevent generating peeling code. If compiled with GCC, we use the -fno-tree-loop-distribute-patterns option to prevent the compiler to replace the inner loop with a call to memset
static __force_inline void GoLGrid_int_clear_column_range (GoLGrid *gg, s32 col_on, s32 col_off, s32 row_on, s32 row_off)
{
	// Caller is responsible for sanity check
	
	u64 col_offset = align_down_u64 (gg->col_offset, PREFERRED_VECTOR_BYTE_SIZE / sizeof (u64));
	u64 *entry = align_down_pointer (gg->grid + (col_offset * (u64) col_on) + (u64) row_on, PREFERRED_VECTOR_BYTE_SIZE);
	
	s32 row_cnt = row_off - row_on;
	
	s32 col_ix;
	s32 row_ix;
	for (col_ix = col_on; col_ix < col_off; col_ix++)
	{
		for (row_ix = 0; row_ix < row_cnt; row_ix++)
			entry [row_ix] = 0;
		
		entry += col_offset;
	}
}

// Clears all non-empty area in the grid that would be unaffected by a copy-type operation to the specified part. The grid may not be entirely empty when this function is called
// The population limits are not changed, this is the responsibility of the calling function
// copy_row_on and copy_row_off will be aligned down and up respectively, according to the expected vector size
static __noinline void GoLGrid_int_clear_unaffected_area (GoLGrid *gg, s32 copy_col_on, s32 copy_col_off, s32 copy_row_on, s32 copy_row_off)
{
	// Caller is responsible for sanity check
	
	s32 clear_col_on = gg->pop_x_on >> 6;
	s32 clear_col_off = (gg->pop_x_off + 63) >> 6;
	
	copy_row_on = align_down_s32 (copy_row_on, PREFERRED_VECTOR_BYTE_SIZE / sizeof (u64));
	copy_row_off = align_up_s32 (copy_row_off, PREFERRED_VECTOR_BYTE_SIZE / sizeof (u64));
	
	s32 clear_row_on = align_down_s32 (gg->pop_y_on, PREFERRED_VECTOR_BYTE_SIZE / sizeof (u64));
	s32 clear_row_off = align_up_s32 (gg->pop_y_off, PREFERRED_VECTOR_BYTE_SIZE / sizeof (u64));
	
	if (clear_col_on < copy_col_on)
		GoLGrid_int_clear_column_range (gg, clear_col_on, copy_col_on, clear_row_on, clear_row_off);
	
	if (copy_col_off < clear_col_off)
		GoLGrid_int_clear_column_range (gg, copy_col_off, clear_col_off, clear_row_on, clear_row_off);
	
	// These will sometimes clear the corners twice, but that is likely to be too rare to make it worthwhile to test for it
	if (gg->pop_y_on < copy_row_on)
		GoLGrid_int_clear_column_range (gg, clear_col_on, clear_col_off, clear_row_on, copy_row_on);
	
	if (copy_row_off < gg->pop_y_off)
		GoLGrid_int_clear_column_range (gg, clear_col_on, clear_col_off, copy_row_off, clear_row_off);
}

static __force_inline void GoLGrid_int_copy_strip_to_column (const u64 *src_entry_left, const u64 *src_entry_right, u64 *dst_entry, int bit_offset, s32 row_cnt)
{
	// Caller is responsible for sanity check
	
	s32 row_ix;
	
	#ifdef __GNUC__
	#pragma GCC ivdep
	#endif
	
	for (row_ix = 0; row_ix < row_cnt; row_ix++)
		dst_entry [row_ix] = (src_entry_left [row_ix] << bit_offset) | (src_entry_right [row_ix] >> (64 - bit_offset));
}

static __force_inline void GoLGrid_int_copy_left_strip_to_column (const u64 *src_entry_left, u64 *dst_entry, int bit_offset, s32 row_cnt)
{
	// Caller is responsible for sanity check
	
	s32 row_ix;
	
	#ifdef __GNUC__
	#pragma GCC ivdep
	#endif
	
	for (row_ix = 0; row_ix < row_cnt; row_ix++)
		dst_entry [row_ix] = src_entry_left [row_ix] << bit_offset;
}

static __force_inline void GoLGrid_int_copy_right_strip_to_column (const u64 *src_entry_right, u64 *dst_entry, int bit_offset, s32 row_cnt)
{
	// Caller is responsible for sanity check
	
	s32 row_ix;
	
	#ifdef __GNUC__
	#pragma GCC ivdep
	#endif
	
	for (row_ix = 0; row_ix < row_cnt; row_ix++)
		dst_entry [row_ix] = src_entry_right [row_ix] >> (64 - bit_offset);
}

static __force_inline void GoLGrid_int_bit_reverse_strip_to_column (const u64 *src_entry_left, const u64 *src_entry_right, u64 *dst_entry, int bit_offset, s32 row_cnt)
{
	// Caller is responsible for sanity check
	
	s32 row_ix;
	
	#ifdef __GNUC__
	#pragma GCC ivdep
	#endif
	
	for (row_ix = 0; row_ix < row_cnt; row_ix++)
		dst_entry [row_ix] = bit_reverse_u64 ((src_entry_left [row_ix] << bit_offset) | (src_entry_right [row_ix] >> (64 - bit_offset)));
}


static __force_inline void GoLGrid_int_bit_reverse_right_strip_to_column (const u64 *src_entry_right, u64 *dst_entry, int bit_offset, s32 row_cnt)
{
	// Caller is responsible for sanity check
	
	s32 row_ix;
	
	#ifdef __GNUC__
	#pragma GCC ivdep
	#endif
	
	for (row_ix = 0; row_ix < row_cnt; row_ix++)
		dst_entry [row_ix] = bit_reverse_u64 (src_entry_right [row_ix] >> (64 - bit_offset));
}

static __force_inline u64 GoLGrid_int_pack_subwords_to_u64 (const u64 *entry, int subword_ix)
{
	int shift = (3 - subword_ix) << 4;
	return (((u64) ((u16) (entry [0] >> shift))) << 48) | (((u64) ((u16) (entry [1] >> shift))) << 32) | (((u64) ((u16) (entry [2] >> shift))) << 16) | ((u64) ((u16) (entry [3] >> shift)));
}

static __force_inline void GoLGrid_int_fetch_16_by_16_block (const u64 *entry, int subword_ix, u64 *word_0, u64 *word_1, u64 *word_2, u64 *word_3)
{
	*word_0 = GoLGrid_int_pack_subwords_to_u64 (entry, subword_ix);
	*word_1 = GoLGrid_int_pack_subwords_to_u64 (entry + 4, subword_ix);
	*word_2 = GoLGrid_int_pack_subwords_to_u64 (entry + 8, subword_ix);
	*word_3 = GoLGrid_int_pack_subwords_to_u64 (entry + 12, subword_ix);
}

static __force_inline void GoLGrid_int_write_subwords_from_u64 (u64 word, u64 *entry, int subword_ix)
{
	int shift = (3 - subword_ix) << 4;
	u64 mask = ~(((u64) 0xffffu) << shift);
	
	entry [0] = (entry [0] & mask) | (((u64) ((u16) (word >> 48))) << shift);
	entry [1] = (entry [1] & mask) | (((u64) ((u16) (word >> 32))) << shift);
	entry [2] = (entry [2] & mask) | (((u64) ((u16) (word >> 16))) << shift);
	entry [3] = (entry [3] & mask) | (((u64) ((u16) word)) << shift);
}

static __force_inline void GoLGrid_int_write_16_by_16_block (u64 word_0, u64 word_1, u64 word_2, u64 word_3, u64 *entry, int subword_ix)
{
	GoLGrid_int_write_subwords_from_u64 (word_0, entry, subword_ix);
	GoLGrid_int_write_subwords_from_u64 (word_1, entry + 4, subword_ix);
	GoLGrid_int_write_subwords_from_u64 (word_2, entry + 8, subword_ix);
	GoLGrid_int_write_subwords_from_u64 (word_3, entry + 12, subword_ix);
}

static __force_inline void GoLGrid_int_flip_diagonally_16_by_16_block (u64 *word_0, u64 *word_1, u64 *word_2, u64 *word_3)
{
	u64 w0 = (*word_0 & 0xaaaa5555aaaa5555u) | ((*word_0 & 0x5555000055550000u) >> 15) | ((*word_0 & 0x0000aaaa0000aaaau) << 15);
	u64 w1 = (*word_1 & 0xaaaa5555aaaa5555u) | ((*word_1 & 0x5555000055550000u) >> 15) | ((*word_1 & 0x0000aaaa0000aaaau) << 15);
	u64 w2 = (*word_2 & 0xaaaa5555aaaa5555u) | ((*word_2 & 0x5555000055550000u) >> 15) | ((*word_2 & 0x0000aaaa0000aaaau) << 15);
	u64 w3 = (*word_3 & 0xaaaa5555aaaa5555u) | ((*word_3 & 0x5555000055550000u) >> 15) | ((*word_3 & 0x0000aaaa0000aaaau) << 15);
	
	w0 = (w0 & 0xcccccccc33333333u) | ((w0 & 0x3333333300000000u) >> 30) | ((w0 & 0x00000000ccccccccu) << 30);
	w1 = (w1 & 0xcccccccc33333333u) | ((w1 & 0x3333333300000000u) >> 30) | ((w1 & 0x00000000ccccccccu) << 30);
	w2 = (w2 & 0xcccccccc33333333u) | ((w2 & 0x3333333300000000u) >> 30) | ((w2 & 0x00000000ccccccccu) << 30);
	w3 = (w3 & 0xcccccccc33333333u) | ((w3 & 0x3333333300000000u) >> 30) | ((w3 & 0x00000000ccccccccu) << 30);
	
	u64 t0 = w0;
	u64 t2 = w2;
	
	w0 = (w0 & 0xf0f0f0f0f0f0f0f0u) | ((w1 & 0xf0f0f0f0f0f0f0f0u) >> 4);
	w1 = (w1 & 0x0f0f0f0f0f0f0f0fu) | ((t0 & 0x0f0f0f0f0f0f0f0fu) << 4);
	w2 = (w2 & 0xf0f0f0f0f0f0f0f0u) | ((w3 & 0xf0f0f0f0f0f0f0f0u) >> 4);
	w3 = (w3 & 0x0f0f0f0f0f0f0f0fu) | ((t2 & 0x0f0f0f0f0f0f0f0fu) << 4);
	
	*word_0 = (w0 & 0xff00ff00ff00ff00u) | ((w2 & 0xff00ff00ff00ff00u) >> 8);
	*word_1 = (w1 & 0xff00ff00ff00ff00u) | ((w3 & 0xff00ff00ff00ff00u) >> 8);
	*word_2 = (w2 & 0x00ff00ff00ff00ffu) | ((w0 & 0x00ff00ff00ff00ffu) << 8);
	*word_3 = (w3 & 0x00ff00ff00ff00ffu) | ((w1 & 0x00ff00ff00ff00ffu) << 8);
}

static __force_inline u64 GoLGrid_int_evolve_word (u64 upper_word, u64 mid_word, u64 lower_word)
{
	// Caller is responsible for sanity check
	
	u64 nb_to_add = (upper_word >> 1);
	u64 nb_sum_bit_0 = nb_to_add;
	
	nb_to_add = upper_word;
	u64 nb_sum_bit_1 = nb_sum_bit_0 & nb_to_add;
	nb_sum_bit_0 = nb_sum_bit_0 ^ nb_to_add;

	nb_to_add = (upper_word << 1);
	u64 carry_0_to_1 = nb_sum_bit_0 & nb_to_add;
	nb_sum_bit_1 = nb_sum_bit_1 | carry_0_to_1;
	nb_sum_bit_0 = nb_sum_bit_0 ^ nb_to_add;

	nb_to_add = (mid_word >> 1);
	carry_0_to_1 = nb_sum_bit_0 & nb_to_add;
	u64 nb_sum_overflow = nb_sum_bit_1 & carry_0_to_1;
	nb_sum_bit_1 = nb_sum_bit_1 ^ carry_0_to_1;
	nb_sum_bit_0 = nb_sum_bit_0 ^ nb_to_add;

	nb_to_add = (mid_word << 1);
	carry_0_to_1 = nb_sum_bit_0 & nb_to_add;
	u64 carry_1_to_2 = nb_sum_bit_1 & carry_0_to_1;
	nb_sum_overflow = nb_sum_overflow | carry_1_to_2;
	nb_sum_bit_1 = nb_sum_bit_1 ^ carry_0_to_1;
	nb_sum_bit_0 = nb_sum_bit_0 ^ nb_to_add;

	nb_to_add = (lower_word >> 1);
	carry_0_to_1 = nb_sum_bit_0 & nb_to_add;
	carry_1_to_2 = nb_sum_bit_1 & carry_0_to_1;
	nb_sum_overflow = nb_sum_overflow | carry_1_to_2;
	nb_sum_bit_1 = nb_sum_bit_1 ^ carry_0_to_1;
	nb_sum_bit_0 = nb_sum_bit_0 ^ nb_to_add;

	nb_to_add = lower_word;
	carry_0_to_1 = nb_sum_bit_0 & nb_to_add;
	carry_1_to_2 = nb_sum_bit_1 & carry_0_to_1;
	nb_sum_overflow = nb_sum_overflow | carry_1_to_2;
	nb_sum_bit_1 = nb_sum_bit_1 ^ carry_0_to_1;
	nb_sum_bit_0 = nb_sum_bit_0 ^ nb_to_add;
	
	nb_to_add = (lower_word << 1);
	carry_0_to_1 = nb_sum_bit_0 & nb_to_add;
	carry_1_to_2 = nb_sum_bit_1 & carry_0_to_1;
	nb_sum_overflow = nb_sum_overflow | carry_1_to_2;
	nb_sum_bit_1 = nb_sum_bit_1 ^ carry_0_to_1;
	nb_sum_bit_0 = nb_sum_bit_0 ^ nb_to_add;
	
	return ~(nb_sum_overflow) & nb_sum_bit_1 & (nb_sum_bit_0 | mid_word);
}

// Each of the following four functions evolves a vertical slice of a grid, with some variations
// The following considerations are optimizations for GCC specifically, to allow clean vectorization, without breaking portablity to other compilers:
// Grid entry pointers and row_cnt should be aligned according to the natural alignment of the expected vector size. This is done _outside_ of these functions, and that information is
// propagated into them as a result of function inlining. This convinces GCC that there is no need to generate peeling code before or after the main loop.
// GCC has a problem with moving these alignment operations _into_ the functions (it works for the pointers but not for row_cnt). My guess is this is because the _column function
// and the corresponding _strip function are called in an if-else-clause in GoLGrid_evolve, choosing one or the other. If the alignment of row_cnt is inside the function,
// GCC notices after inlining that this calculation is invariant to the if-else-clause and moves it outside of it. Appearently this makes GCC forget about the alignment properties of
// row_cnt, and makes it generate extra code for the last few iterations of the loop in each function (peeling code)
// Also, for GCC to be able to vectorize the loop at all, all needed memory words are read in each iteration, instead of recycling those from the previous iteration as possible.
// "#pragma GCC ivdep" tells GCC that memory areas accessed through in_entry and out_entry don't overlap, so there is no need to generate aliasing checks

static __force_inline u64 GoLGrid_int_evolve_column (const u64 *in_entry, u64 *out_entry, s32 row_cnt)
{
	// Caller is responsible for sanity check
	
	u64 or_of_result = 0;
	s32 row_ix;
	
	#ifdef __GNUC__
	#pragma GCC ivdep
	#endif
	
	for (row_ix = 0; row_ix < row_cnt; row_ix++)
	{
		u64 out_word = GoLGrid_int_evolve_word (in_entry [-1], in_entry [0], in_entry [1]);
		in_entry++;
		
		*out_entry++ = out_word;
		or_of_result |= out_word;
	}
	
	return or_of_result;
}

static __force_inline u64 GoLGrid_int_evolve_strip (const u64 *in_entry_left, const u64 *in_entry_right, u64 *out_entry_left, u64 *out_entry_right, int bit_offset, s32 row_cnt)
{
	// Caller is responsible for sanity check
	
	u64 or_of_result = 0;
	s32 row_ix;
	
	#ifdef __GNUC__
	#pragma GCC ivdep
	#endif
	
	for (row_ix = 0; row_ix < row_cnt; row_ix++)
	{
		u64 upper_word = (in_entry_left [-1] << bit_offset) | (in_entry_right [-1] >> (64 - bit_offset));
		u64 mid_word = (in_entry_left [0] << bit_offset) | (in_entry_right [0] >> (64 - bit_offset));
		u64 lower_word = (in_entry_left [1] << bit_offset) | (in_entry_right [1] >> (64 - bit_offset));
		in_entry_left++;
		in_entry_right++;
		
		u64 out_word = GoLGrid_int_evolve_word (upper_word, mid_word, lower_word);
		*out_entry_left++ = (out_word >> bit_offset);
		*out_entry_right++ = (out_word << (64 - bit_offset));
		
		or_of_result |= out_word;
	}
	
	return or_of_result;
}

static __force_inline u64 GoLGrid_int_evolve_column_merge (const u64 *in_entry, u64 *out_entry, s32 row_cnt)
{
	// Caller is responsible for sanity check
	
	u64 or_of_result = 0;
	s32 row_ix;
	
	#ifdef __GNUC__
	#pragma GCC ivdep
	#endif
	
	for (row_ix = 0; row_ix < row_cnt; row_ix++)
	{
		u64 out_word = GoLGrid_int_evolve_word (in_entry [-1], in_entry [0], in_entry [1]) & 0x7fffffffffffffffu;
		in_entry++;
		
		out_entry [0] = (out_entry [0] & 0x8000000000000000u) | out_word;
		out_entry++;
		
		or_of_result |= out_word;
	}
	
	return or_of_result;
}

static __force_inline u64 GoLGrid_int_evolve_strip_merge (const u64 *in_entry_left, const u64 *in_entry_right, u64 *out_entry_left, u64 *out_entry_right, int bit_offset, s32 row_cnt)
{
	// Caller is responsible for sanity check
	
	u64 or_of_result = 0;
	s32 row_ix;
	
	#ifdef __GNUC__
	#pragma GCC ivdep
	#endif
	
	for (row_ix = 0; row_ix < row_cnt; row_ix++)
	{
		u64 upper_word = (in_entry_left [-1] << bit_offset) | (in_entry_right [-1] >> (64 - bit_offset));
		u64 mid_word = (in_entry_left [0] << bit_offset) | (in_entry_right [0] >> (64 - bit_offset));
		u64 lower_word = (in_entry_left [1] << bit_offset) | (in_entry_right [1] >> (64 - bit_offset));
		in_entry_left++;
		in_entry_right++;
		
		u64 out_word = GoLGrid_int_evolve_word (upper_word, mid_word, lower_word) & 0x7fffffffffffffffu;
		or_of_result |= out_word;
		
		out_entry_left [0] = (out_entry_left [0] & (((u64) 0xffffffffffffffffu) << (63 - bit_offset))) | (out_word >> bit_offset);
		out_entry_right [0] = out_word << (64 - bit_offset);
		out_entry_left++;
		out_entry_right++;
	}
	
	return or_of_result;
}


// External functions

static __noinline void GoLGrid_free (GoLGrid *gg)
{
	if (!gg)
		return (void) ffsc (__func__);
	
	if (gg->grid_alloc)
		free (gg->grid_alloc);
	
	GoLGrid_int_preinit (gg);
}

static __noinline int GoLGrid_create (GoLGrid *gg, const Rect *grid_rect)
{
	if (!gg)
		return ffsc (__func__);
	
	GoLGrid_int_preinit (gg);
	
	if (!grid_rect || (GOLGRID_HEIGHT_GRANULARITY * sizeof (u64) < MAX_SUPPORTED_VECTOR_BYTE_SIZE) || grid_rect->width <= 0 || (grid_rect->width % GOLGRID_WIDTH_GRANULARITY) != 0 ||
			grid_rect->height <= 0 || (grid_rect->height % GOLGRID_HEIGHT_GRANULARITY) != 0)
		return ffsc (__func__);
	
	Rect_copy (grid_rect, &gg->grid_rect);
	
	s32 column_cnt = gg->grid_rect.width >> 6;
	s32 single_column_byte_size = (2 * PREFERRED_VECTOR_BYTE_SIZE) + (grid_rect->height * sizeof (u64));
	s32 extra_column_byte_size = PREFERRED_VECTOR_BYTE_SIZE + (grid_rect->height * sizeof (u64));
	
	s32 column_byte_offset = align_up_s32 (extra_column_byte_size, MAX_SUPPORTED_VECTOR_BYTE_SIZE);
	u64 grid_buffer_size = (u64) single_column_byte_size + ((u64) (column_cnt - 1) * (u64) column_byte_offset);
	
	if (!allocate_aligned (grid_buffer_size, MAX_SUPPORTED_VECTOR_BYTE_SIZE, MAX_SUPPORTED_VECTOR_BYTE_SIZE - PREFERRED_VECTOR_BYTE_SIZE, TRUE, (void **) &gg->grid_alloc, (void **) &gg->grid))
	{
		fprintf (stderr, "Out of memory in %s\n", __func__);
		GoLGrid_free (gg);
		return FALSE;
	}
	
	gg->grid += (PREFERRED_VECTOR_BYTE_SIZE / sizeof (u64));
	gg->col_offset = column_byte_offset / sizeof (u64);
	
	GoLGrid_int_set_empty_population_rect (gg);
	gg->generation = 0;
	
	return TRUE;
}

static __force_inline void GoLGrid_get_grid_rect (const GoLGrid *gg, Rect *grid_rect)
{
	if (!gg || !grid_rect)
		return (void) ffsc (__func__);
	
	Rect_copy (&gg->grid_rect, grid_rect);
}

static __force_inline void GoLGrid_set_grid_coords (GoLGrid *gg, s32 left_x, s32 top_y)
{
	if (!gg)
		return (void) ffsc (__func__);
	
	gg->grid_rect.left_x = left_x;
	gg->grid_rect.top_y = top_y;
}

// Returns FALSE if grid was empty
static __force_inline int GoLGrid_get_bounding_box (const GoLGrid *gg, Rect *bounding_box)
{
	if (!gg || !bounding_box)
	{
		if (bounding_box)
			Rect_make (bounding_box, 0, 0, 0, 0);
		
		return ffsc (__func__);
	}
	
	Rect_make (bounding_box, gg->pop_x_on + gg->grid_rect.left_x, gg->pop_y_on + gg->grid_rect.top_y, gg->pop_x_off - gg->pop_x_on, gg->pop_y_off - gg->pop_y_on);
	
	return (gg->pop_x_off > gg->pop_x_on);
}

static __force_inline s64 GoLGrid_get_generation (const GoLGrid *gg)
{
	if (!gg)
		return ffsc (__func__);
	
	return gg->generation;
}

static __force_inline void GoLGrid_set_generation (GoLGrid *gg, s64 generation)
{
	if (!gg)
		return (void) ffsc (__func__);
	
	gg->generation = generation;
}

static __force_inline int GoLGrid_is_empty (const GoLGrid *gg)
{
	if (!gg)
		return ffsc (__func__);
	
	return (gg->pop_x_off <= gg->pop_x_on);
}

// Returns 0 (off-cell) if (x, y) is outside the grid
static __force_inline int GoLGrid_get_cell (const GoLGrid *gg, s32 x, s32 y)
{
	if (!gg || !gg->grid)
		return ffsc (__func__);
	
	s32 phys_x = x - gg->grid_rect.left_x;
	s32 phys_y = y - gg->grid_rect.top_y;
	
	// Optimized test of (left_x <= x < left_x + width) and (top_y <= y < top_y + height)
	if ((u32) phys_x >= (u32) gg->grid_rect.width || (u32) phys_y >= (u32) gg->grid_rect.height)
		return 0;
	
	return (gg->grid [gg->col_offset * (((u64) phys_x) >> 6) + (u64) phys_y] >> (63 - (((u64) phys_x) & 0x3f))) & 1;
}

// Returns FALSE if (x, y) is outside the grid
static __force_inline int GoLGrid_set_cell_on (GoLGrid *gg, s32 x, s32 y)
{
	if (!gg || !gg->grid)
		return ffsc (__func__);
	
	s32 phys_x = x - gg->grid_rect.left_x;
	s32 phys_y = y - gg->grid_rect.top_y;
	
	if ((u32) phys_x >= (u32) gg->grid_rect.width || (u32) phys_y >= (u32) gg->grid_rect.height)
		return FALSE;
	
	gg->grid [gg->col_offset * (((u64) phys_x) >> 6) + (u64) phys_y] |= ((u64) 1) << (63 - (((u64) phys_x) & 0x3f));
	GoLGrid_int_adjust_pop_rect_new_on_cell (gg, phys_x, phys_y);
	
	return TRUE;
}

static __noinline int GoLGrid_set_cell_off (GoLGrid *gg, s32 x, s32 y)
{
	if (!gg || !gg->grid)
		return ffsc (__func__);
	
	s32 phys_x = x - gg->grid_rect.left_x;
	s32 phys_y = y - gg->grid_rect.top_y;
	
	if ((u32) phys_x >= (u32) gg->grid_rect.width || (u32) phys_y >= (u32) gg->grid_rect.height)
		return FALSE;
	
	gg->grid [gg->col_offset * (((u64) phys_x) >> 6) + (u64) phys_y] &= ~((u64) 1) << (63 - (((u64) phys_x) & 0x3f));
	GoLGrid_int_adjust_pop_rect_new_off_cell (gg, phys_x, phys_y);
	
	return TRUE;
}

// Or an 8-by-8 block of cells from a u64 value into the grid. left_x and top_y must be aligned (mod 8) to the left_x and top_y of the grid
// Bit 63 of the u64 corresponds to the top-left corner of the block, bit 56 to the top_right corner etc.
static __force_inline int GoLGrid_or_8_by_8_block (GoLGrid *gg, s32 left_x, s32 top_y, u64 bits)
{
	if (!gg || !gg->grid)
		return ffsc (__func__);
	
	s32 phys_left_x = left_x - gg->grid_rect.left_x;
	s32 phys_top_y = top_y - gg->grid_rect.top_y;
	
	if ((phys_left_x & 0x07) != 0 || (phys_top_y & 0x07) != 0)
		return ffsc (__func__);
	
	if ((u32) phys_left_x >= (u32) gg->grid_rect.width || (u32) phys_top_y >= (u32) gg->grid_rect.height)
		return FALSE;
	
	u64 *entry = gg->grid + (gg->col_offset * (((u64) phys_left_x) >> 6)) + (u64) phys_top_y;
	
	u64 or_of_words = 0;
	s32 row_ix;
	for (row_ix = 0; row_ix < 8; row_ix++)
	{
		u64 word = (bits >> (8 * (7 - row_ix))) & 0xff;
		or_of_words |= word;
		entry [row_ix] |= (word << (56 - (phys_left_x & 0x38)));
	}
	
	if (or_of_words != 0)
	{
		s32 ored_x_on = phys_left_x + (7 - most_significant_bit_u64 (or_of_words));
		s32 ored_x_off = phys_left_x + (8 - least_significant_bit_u64 (or_of_words));
		s32 ored_y_on = phys_top_y + (7 - (most_significant_bit_u64 (bits) >> 3));
		s32 ored_y_off = phys_top_y + (8 - (least_significant_bit_u64 (bits) >> 3));
		GoLGrid_int_adjust_pop_rect_ored_bounding_box (gg, ored_x_on, ored_x_off, ored_y_on, ored_y_off);
	}
	
	return TRUE;
}

static __force_inline void GoLGrid_clear (GoLGrid *gg)
{
	if (!gg || !gg->grid)
		return (void) ffsc (__func__);
	
	gg->generation = 0;
	
	if (gg->pop_x_off <= gg->pop_x_on)
		return;
	
	s32 col_on = gg->pop_x_on >> 6;
	s32 col_off = (gg->pop_x_off + 63) >> 6;
	
	s32 row_on = align_down_s32 (gg->pop_y_on, PREFERRED_VECTOR_BYTE_SIZE / sizeof (u64));
	s32 row_off = align_up_s32 (gg->pop_y_off, PREFERRED_VECTOR_BYTE_SIZE / sizeof (u64));
	
	GoLGrid_int_clear_column_range (gg, col_on, col_off, row_on, row_off);
	GoLGrid_int_set_empty_population_rect (gg);
}

// This is based on the public domain MurmurHash by Austin Appleby, but instead of multiplying the current hash with a constant for each word (which prevents vectorization)
// each word in the key in xored with a random word which is specific for the position of that key word in the grid
// Note when comparing hash values, that they are generated using physical (not virtual) coordinates
static __force_inline u64 GoLGrid_get_hash (const GoLGrid *gg, const RandomDataArray *rda)
{
	if (!gg || !gg->grid || !rda || !RandomDataArray_verify_size (rda, (u64) (gg->grid_rect.width >> 6) * (u64) gg->grid_rect.height))
		return ffsc (__func__);
	
	u64 hash = 0x0123456789abcdefu;
	
	if (gg->pop_x_off <= gg->pop_x_on)
		return hash ^ (hash >> 47);
	
	s32 col_on = gg->pop_x_on >> 6;
	s32 col_off = (gg->pop_x_off + 63) >> 6;
	
	s32 row_on = align_down_s32 (gg->pop_y_on, PREFERRED_VECTOR_BYTE_SIZE / sizeof (u64));
	s32 row_off = align_up_s32 (gg->pop_y_off, PREFERRED_VECTOR_BYTE_SIZE / sizeof (u64));
	s32 row_cnt = row_off - row_on;
	
	u64 grid_col_offset = align_down_u64 (gg->col_offset, PREFERRED_VECTOR_BYTE_SIZE / sizeof (u64));
	u64 rda_col_offset = align_down_u64 ((u64) gg->grid_rect.height, PREFERRED_VECTOR_BYTE_SIZE / sizeof (u64));
	
	const u64 *grid_entry = align_down_const_pointer (gg->grid + (grid_col_offset * (u64) col_on) + (u64) row_on, PREFERRED_VECTOR_BYTE_SIZE);
	const u64 *rda_entry = align_down_const_pointer (rda->random_data + (rda_col_offset * (u64) col_on) + (u64) row_on, PREFERRED_VECTOR_BYTE_SIZE);
	
	s32 col_ix;
	s32 row_ix;
	for (col_ix = col_on; col_ix < col_off; col_ix++)
	{
		for (row_ix = 0; row_ix < row_cnt; row_ix++)
		{
			u64 grid_word = grid_entry [row_ix];
			u64 random_word = rda_entry [row_ix];
			u64 key_word = grid_word ^ random_word;
			
			key_word = key_word * 0xc6a4a7935bd1e995u;
			key_word = key_word ^ (key_word >> 47);
			key_word = key_word * 0xc6a4a7935bd1e995u;
			
			hash = hash ^ key_word;
		}
		
		grid_entry += grid_col_offset;
		rda_entry += rda_col_offset;
	}
	
	return hash ^ (hash >> 47);
}

static __force_inline u64 GoLGrid_get_population (const GoLGrid *gg)
{
	if (!gg || !gg->grid)
		return ffsc (__func__);
	
	u64 population = 0;
	if (gg->pop_x_off <= gg->pop_x_on)
		return population;
	
	s32 col_on = gg->pop_x_on >> 6;
	s32 col_off = (gg->pop_x_off + 63) >> 6;
	
	s32 row_on = align_down_s32 (gg->pop_y_on, PREFERRED_VECTOR_BYTE_SIZE / sizeof (u64));
	s32 row_off = align_up_s32 (gg->pop_y_off, PREFERRED_VECTOR_BYTE_SIZE / sizeof (u64));
	s32 row_cnt = row_off - row_on;
	
	u64 col_offset = align_down_u64 (gg->col_offset, PREFERRED_VECTOR_BYTE_SIZE / sizeof (u64));
	const u64 *entry = align_down_const_pointer (gg->grid + (col_offset * (u64) col_on) + (u64) row_on, PREFERRED_VECTOR_BYTE_SIZE);
	
	s32 col_ix;
	s32 row_ix;
	for (col_ix = col_on; col_ix < col_off; col_ix++)
	{
		for (row_ix = 0; row_ix < row_cnt; row_ix++)
			population += bit_count_u64 (entry [row_ix]);
		
		entry += col_offset;
	}
	
	return population;
}

// src_gg and dst_gg must have the same grid_rect size and position
static __force_inline void GoLGrid_or (const GoLGrid *src_gg, GoLGrid *dst_gg)
{
	if (!src_gg || !src_gg->grid || !dst_gg || !dst_gg->grid || !Rect_is_equal (&dst_gg->grid_rect, &src_gg->grid_rect))
		return (void) ffsc (__func__);
	
	if (src_gg->pop_x_off <= src_gg->pop_x_on)
		return;
	
	s32 col_on = src_gg->pop_x_on >> 6;
	s32 col_off = (src_gg->pop_x_off + 63) >> 6;
	
	s32 row_on = align_down_s32 (src_gg->pop_y_on, PREFERRED_VECTOR_BYTE_SIZE / sizeof (u64));
	s32 row_off = align_up_s32 (src_gg->pop_y_off, PREFERRED_VECTOR_BYTE_SIZE / sizeof (u64));
	s32 row_cnt = row_off - row_on;
	
	u64 col_offset = align_down_u64 (src_gg->col_offset, PREFERRED_VECTOR_BYTE_SIZE / sizeof (u64));
	const u64 *src_entry = align_down_const_pointer (src_gg->grid + (col_offset * (u64) col_on) + (u64) row_on, PREFERRED_VECTOR_BYTE_SIZE);
	u64 *dst_entry = align_down_pointer (dst_gg->grid + (col_offset * (u64) col_on) + (u64) row_on, PREFERRED_VECTOR_BYTE_SIZE);
	
	s32 col_ix;
	s32 row_ix;
	
	for (col_ix = col_on; col_ix < col_off; col_ix++)
	{
		#ifdef __GNUC__
		#pragma GCC ivdep
		#endif
		
		for (row_ix = 0; row_ix < row_cnt; row_ix++)
			dst_entry [row_ix] |= src_entry [row_ix];
		
		src_entry += col_offset;
		dst_entry += col_offset;
	}
	
	GoLGrid_int_adjust_pop_rect_ored_bounding_box (dst_gg, src_gg->pop_x_on, src_gg->pop_x_off, src_gg->pop_y_on, src_gg->pop_y_off);
}

// Makes a copy of src_gg to dst_gg, which may be of a different size. The current virtual position of dst_gg is taken into account, to shift the physical position of the pattern
// while copying. Unaffected parts of dst_gg are cleared, the generation count is copied from src. Clipping of source contents is supported, if this happens the return value is FALSE
static __force_inline int GoLGrid_copy_unmatched (const GoLGrid *src_gg, GoLGrid *dst_gg)
{
	if (!src_gg || !src_gg->grid || !dst_gg || !dst_gg->grid)
		return ffsc (__func__);
	
	dst_gg->generation = src_gg->generation;
	
	if (src_gg->pop_x_off <= src_gg->pop_x_on)
	{
		GoLGrid_clear (dst_gg);
		return TRUE;
	}
	
	s32 delta_x = src_gg->grid_rect.left_x - dst_gg->grid_rect.left_x;
	s32 delta_y = src_gg->grid_rect.top_y - dst_gg->grid_rect.top_y;
	
	s32 dst_bit_on = highest_of_s32 (src_gg->pop_x_on + delta_x, 0);
	s32 dst_bit_off = lowest_of_s32 (src_gg->pop_x_off + delta_x, dst_gg->grid_rect.width);
	s32 required_dst_row_on = highest_of_s32 (src_gg->pop_y_on + delta_y, 0);
	s32 required_dst_row_off = lowest_of_s32 (src_gg->pop_y_off + delta_y, dst_gg->grid_rect.height);
	
	if (dst_bit_on >= dst_bit_off || required_dst_row_on >= required_dst_row_off)
	{
		GoLGrid_clear (dst_gg);
		return FALSE;
	}
	
	s32 dst_col_on = dst_bit_on >> 6;
	s32 dst_col_off = (dst_bit_off + 63) >> 6;
	s32 dst_row_on = align_down_s32 (required_dst_row_on, PREFERRED_VECTOR_BYTE_SIZE / sizeof (u64));
	s32 dst_row_off = align_up_s32 (required_dst_row_off, PREFERRED_VECTOR_BYTE_SIZE / sizeof (u64));
	
	s32 row_cnt = dst_row_off - dst_row_on;
	
	if (dst_gg->pop_x_on < dst_gg->pop_x_off)
	{
		s32 clear_col_on = dst_gg->pop_x_on >> 6;
		s32 clear_col_off = (dst_gg->pop_x_off + 63) >> 6;
		
		if (clear_col_on < dst_col_on || clear_col_off > dst_col_off || dst_gg->pop_y_on < dst_row_on || dst_gg->pop_y_off > dst_row_off)
			GoLGrid_int_clear_unaffected_area (dst_gg, dst_col_on, dst_col_off, dst_row_on, dst_row_off);
	}
	
	s32 src_left_col_on = ((dst_col_on << 6) - delta_x) >> 6;
	s32 src_grid_col_cnt = src_gg->grid_rect.width >> 6;
	
	s32 src_row_on = dst_row_on - delta_y;
	
	u64 src_col_offset = src_gg->col_offset;
	u64 dst_col_offset = align_down_u64 (dst_gg->col_offset, PREFERRED_VECTOR_BYTE_SIZE / sizeof (u64));
	
	const u64 *src_entry_left = src_gg->grid + (src_col_offset * src_left_col_on) + src_row_on;
	u64 *dst_entry = dst_gg->grid + (dst_col_offset * dst_col_on) + dst_row_on;
	dst_entry = align_down_pointer (dst_entry, PREFERRED_VECTOR_BYTE_SIZE);
	
	s32 bit_offset = (-delta_x) & 0x3f;
	
	s32 src_left_col_ix = src_left_col_on;
	s32 dst_col_ix;
	
	for (dst_col_ix = dst_col_on; dst_col_ix < dst_col_off; dst_col_ix++)
	{
		const u64 *src_entry_right = src_entry_left + src_col_offset;
		
		if (bit_offset == 0 || src_left_col_ix >= (src_grid_col_cnt - 1))
			GoLGrid_int_copy_left_strip_to_column (src_entry_left, dst_entry, bit_offset, row_cnt);
		else if (src_left_col_ix < 0)
			GoLGrid_int_copy_right_strip_to_column (src_entry_right, dst_entry, bit_offset, row_cnt);
		else
			GoLGrid_int_copy_strip_to_column (src_entry_left, src_entry_right, dst_entry, bit_offset, row_cnt);
		
		src_left_col_ix++;
		src_entry_left += src_col_offset;
		dst_entry += dst_col_offset;
	}
	
	dst_gg->pop_x_on = dst_bit_on;
	dst_gg->pop_x_off = dst_bit_off;
	dst_gg->pop_y_on = required_dst_row_on;
	dst_gg->pop_y_off = required_dst_row_off;
	
	if (src_gg->pop_x_on + delta_x >= 0 && src_gg->pop_x_off + delta_x <= dst_gg->grid_rect.width && src_gg->pop_y_on + delta_y >= 0 && src_gg->pop_y_off + delta_y <= dst_gg->grid_rect.height)
		return TRUE;
		
	if (!GoLGrid_int_tighten_pop_x_on (dst_gg))
		return FALSE;
	
	GoLGrid_int_tighten_pop_x_off (dst_gg);
	GoLGrid_int_tighten_pop_y_on (dst_gg);
	GoLGrid_int_tighten_pop_y_off (dst_gg);
	
	return FALSE;
}

static __force_inline void GoLGrid_flip_horizontally (const GoLGrid *src_gg, GoLGrid *dst_gg)
{
	if (!src_gg || !src_gg->grid || !dst_gg || !dst_gg->grid || !(Rect_is_equal (&dst_gg->grid_rect, &src_gg->grid_rect)))
		return (void) ffsc (__func__);
	
	if (src_gg->pop_x_off <= src_gg->pop_x_on)
	{
		GoLGrid_clear (dst_gg);
		dst_gg->generation = src_gg->generation;
		return;
	}
	
	if (src_gg->pop_x_on != 0 || src_gg->pop_y_on != 0)
		return (void) ffsc (__func__);
	
	s32 col_off = (src_gg->pop_x_off + 63) >> 6;
	s32 row_off = align_up_s32 (src_gg->pop_y_off, PREFERRED_VECTOR_BYTE_SIZE / sizeof (u64));
	
	if (dst_gg->pop_x_on < dst_gg->pop_x_off)
	{
		s32 clear_col_off = (dst_gg->pop_x_off + 63) >> 6;
		
		if (clear_col_off > col_off || dst_gg->pop_y_off > row_off)
			GoLGrid_int_clear_unaffected_area (dst_gg, 0, col_off, 0, row_off);
	}
	
	u64 col_offset = align_down_u64 (src_gg->col_offset, PREFERRED_VECTOR_BYTE_SIZE / sizeof (u64));
	const u64 *src_entry = align_down_pointer (src_gg->grid + (col_offset * (col_off - 2)), PREFERRED_VECTOR_BYTE_SIZE);
	u64 *dst_entry = align_down_pointer (dst_gg->grid, PREFERRED_VECTOR_BYTE_SIZE);
	
	// bit_offset should be 64 if pop_x_off is aligned to 64
	s32 bit_offset = 1 + ((src_gg->pop_x_off - 1) & 0x3f);
	
	s32 src_left_col_ix = col_off - 2;
	s32 dst_col_ix;
	for (dst_col_ix = 0; dst_col_ix < col_off; dst_col_ix++)
	{
		const u64 *src_entry_right = align_down_const_pointer (src_entry + col_offset, PREFERRED_VECTOR_BYTE_SIZE);
		
		if (bit_offset == 64 || src_left_col_ix < 0)
			GoLGrid_int_bit_reverse_right_strip_to_column (src_entry_right, dst_entry, bit_offset, row_off);
		else
			GoLGrid_int_bit_reverse_strip_to_column (src_entry, src_entry_right, dst_entry, bit_offset, row_off);
		
		src_entry -= col_offset;
		src_left_col_ix--;
		dst_entry += col_offset;
	}
	
	dst_gg->pop_x_on = 0;
	dst_gg->pop_x_off = src_gg->pop_x_off;
	dst_gg->pop_y_on = 0;
	dst_gg->pop_y_off = src_gg->pop_y_off;
	
	dst_gg->generation = src_gg->generation;
}

static __force_inline void GoLGrid_flip_vertically (const GoLGrid *src_gg, GoLGrid *dst_gg)
{
	if (!src_gg || !src_gg->grid || !dst_gg || !dst_gg->grid || !(Rect_is_equal (&dst_gg->grid_rect, &src_gg->grid_rect)))
		return (void) ffsc (__func__);
	
	if (src_gg->pop_x_off <= src_gg->pop_x_on)
	{
		GoLGrid_clear (dst_gg);
		dst_gg->generation = src_gg->generation;
		return;
	}
	
	if (src_gg->pop_x_on != 0 || src_gg->pop_y_on != 0)
		return (void) ffsc (__func__);
	
	s32 col_off = (src_gg->pop_x_off + 63) >> 6;
	s32 row_off = align_up_s32 (src_gg->pop_y_off, PREFERRED_VECTOR_BYTE_SIZE / sizeof (u64));
	
	if (dst_gg->pop_x_on < dst_gg->pop_x_off)
	{
		s32 clear_col_off = (dst_gg->pop_x_off + 63) >> 6;
		
		if (clear_col_off > col_off || dst_gg->pop_y_off > row_off)
			GoLGrid_int_clear_unaffected_area (dst_gg, 0, col_off, 0, row_off);
	}
	
	u64 col_offset = align_down_u64 (src_gg->col_offset, PREFERRED_VECTOR_BYTE_SIZE / sizeof (u64));
	const u64 *src_entry = src_gg->grid + (u64) src_gg->pop_y_off;
	u64 *dst_entry = align_down_pointer (dst_gg->grid, PREFERRED_VECTOR_BYTE_SIZE);
	
	s32 col_ix;
	s32 row_ix;
	for (col_ix = 0; col_ix < col_off; col_ix++)
	{
		#ifdef __GNUC__
		#pragma GCC ivdep
		#endif
		
		for (row_ix = 0; row_ix < row_off; row_ix++)
			dst_entry [row_ix] = src_entry [-1 - row_ix];
		
		src_entry += col_offset;
		dst_entry += col_offset;
	}
	
	dst_gg->pop_x_on = 0;
	dst_gg->pop_x_off = src_gg->pop_x_off;
	dst_gg->pop_y_on = 0;
	dst_gg->pop_y_off = src_gg->pop_y_off;
	
	dst_gg->generation = src_gg->generation;
}

static __force_inline void GoLGrid_flip_diagonally (const GoLGrid *src_gg, GoLGrid *dst_gg)
{
	if (!src_gg || !src_gg->grid || !dst_gg || !dst_gg->grid || src_gg->grid_rect.width != src_gg->grid_rect.height || !(Rect_is_equal (&dst_gg->grid_rect, &src_gg->grid_rect)))
		return (void) ffsc (__func__);
	
	if (src_gg->pop_x_off <= src_gg->pop_x_on)
	{
		GoLGrid_clear (dst_gg);
		dst_gg->generation = src_gg->generation;
		return;
	}
	
	if (src_gg->pop_x_on != 0 || src_gg->pop_y_on != 0)
		return (void) ffsc (__func__);
	
	s32 y_block_cnt = (src_gg->pop_y_off + 15) >> 4;
	s32 x_block_cnt = (src_gg->pop_x_off + 15) >> 4;
	
	if (dst_gg->pop_x_on < dst_gg->pop_x_off)
	{
		s32 clear_col_on = (src_gg->pop_y_off + 15) >> 6;
		s32 clear_row_on = x_block_cnt << 4;
		
		if (dst_gg->pop_x_off > (y_block_cnt << 4) || dst_gg->pop_y_off > clear_row_on)
			GoLGrid_int_clear_unaffected_area (dst_gg, 0, clear_col_on, 0, clear_row_on);
	}
	
	s32 y_block_ix;
	s32 x_block_ix;
	for (y_block_ix = 0; y_block_ix < y_block_cnt; y_block_ix++)
		for (x_block_ix = 0; x_block_ix < x_block_cnt; x_block_ix++)
		{
			s32 src_col = x_block_ix >> 2;
			s32 src_subword = x_block_ix & 0x03;
			s32 src_row = y_block_ix << 4;
			
			s32 dst_col = y_block_ix >> 2;
			s32 dst_subword = y_block_ix & 0x03;
			s32 dst_row = x_block_ix << 4;
			
			const u64 *src_entry = src_gg->grid + (src_gg->col_offset * src_col) + src_row;
			u64 *dst_entry = dst_gg->grid + (dst_gg->col_offset * dst_col) + dst_row;
			
			u64 word_0;
			u64 word_1;
			u64 word_2;
			u64 word_3;
			
			GoLGrid_int_fetch_16_by_16_block (src_entry, src_subword, &word_0, &word_1, &word_2, &word_3);
			GoLGrid_int_flip_diagonally_16_by_16_block (&word_0, &word_1, &word_2, &word_3);
			GoLGrid_int_write_16_by_16_block (word_0, word_1, word_2, word_3, dst_entry, dst_subword);
		}
	
	dst_gg->pop_x_on = 0;
	dst_gg->pop_x_off = src_gg->pop_y_off;
	dst_gg->pop_y_on = 0;
	dst_gg->pop_y_off = src_gg->pop_x_off;
	
	dst_gg->generation = src_gg->generation;
}

// Unaffected parts of out_gg are cleared by this function, because this is more efficient than clearing out_gg explicitly before the call
// in_gg and out_gg must have the same grid_rect size and position
// We use __force_inline here, because of the high overhead of saving and restoring vector registers compared to the fairly small amount of work needed to evolve the populated region of a typical grid
// In less time critical places, use GoLGrid_evolve__noinline instead
static __force_inline void GoLGrid_evolve (const GoLGrid *in_gg, GoLGrid *out_gg)
{
	if (!in_gg || !in_gg->grid || !out_gg || !out_gg->grid || out_gg->grid_rect.width != in_gg->grid_rect.width || out_gg->grid_rect.height != in_gg->grid_rect.height)
		return (void) ffsc (__func__);
	
	out_gg->grid_rect.left_x = in_gg->grid_rect.left_x;
	out_gg->grid_rect.top_y = in_gg->grid_rect.top_y;
	out_gg->generation = in_gg->generation + 1;
	
	// Is the input grid empty?
	if (in_gg->pop_x_off <= in_gg->pop_x_on)
	{
		GoLGrid_clear (out_gg);
		return;
	}
	
	s32 make_bit_on = highest_of_s32 (in_gg->pop_x_on - 1, 0);
	s32 make_bit_off = lowest_of_s32 (in_gg->pop_x_off + 1, in_gg->grid_rect.width);
	s32 make_col_on = make_bit_on >> 6;
	s32 make_col_off = (make_bit_off + 63) >> 6;
	
	s32 required_row_on = highest_of_s32 (in_gg->pop_y_on - 1, 0);
	s32 required_row_off = lowest_of_s32 (in_gg->pop_y_off + 1, in_gg->grid_rect.height);
	s32 make_row_on = align_down_s32 (required_row_on, PREFERRED_VECTOR_BYTE_SIZE / sizeof (u64));
	s32 make_row_off = align_up_s32 (required_row_off, PREFERRED_VECTOR_BYTE_SIZE / sizeof (u64));
	
	// GCC needs a clean variable for this to not move the calculation outside the if-else-clauses, which seems to make it forget about the alignment again
	s32 make_row_cnt = make_row_off - make_row_on;
	
	// If the out grid was non-empty, clear those parts that will not be overwritten by the generated evolved pattern
	// Note that it is rare (less than 1% probability) that any clearing is needed at all, if two grids are alternated as in and out grid when evolving a pattern
	if (out_gg->pop_x_on < out_gg->pop_x_off)
	{
		s32 clear_col_on = out_gg->pop_x_on >> 6;
		s32 clear_col_off = (out_gg->pop_x_off + 63) >> 6;
		
		if (clear_col_on < make_col_on || clear_col_off > make_col_off || out_gg->pop_y_on < make_row_on || out_gg->pop_y_off > make_row_off)
			GoLGrid_int_clear_unaffected_area (out_gg, make_col_on, make_col_off, make_row_on, make_row_off);
	}
	
	// Because we have compared the grid_rect size of in_gg and out_gg we know that they must also have the same col_offset, so we just need one variable
	u64 col_offset = align_down_u64 (in_gg->col_offset, PREFERRED_VECTOR_BYTE_SIZE / sizeof (u64));
	
	const u64 *in_entry = in_gg->grid + (col_offset * (u64) make_col_on) + (u64) make_row_on;
	u64 *out_entry = out_gg->grid + (col_offset * (u64) make_col_on) + (u64) make_row_on;
	
	// These don't change the pointers, but are needed to convince GCC that the pointers are (already) fully aligned to the vector size
	in_entry = align_down_const_pointer (in_entry, PREFERRED_VECTOR_BYTE_SIZE);
	out_entry = align_down_pointer (out_entry, PREFERRED_VECTOR_BYTE_SIZE);
	
	s32 strip_start_bit;
	u64 or_of_result;
	
	// Generate the first strip in the evolved grid
	if (make_col_off - make_col_on == 1 || (make_bit_on & 0x3f) == 0)
	{
		strip_start_bit = make_bit_on & 0xffffffc0;
		or_of_result = GoLGrid_int_evolve_column (in_entry, out_entry, make_row_cnt);
	}
	else
	{
		// Once again we must convince GCC that that the sum of two aligned values is still aligned
		const u64 *in_entry_right = align_down_const_pointer (in_entry + col_offset, PREFERRED_VECTOR_BYTE_SIZE);
		u64 *out_entry_right = align_down_pointer (out_entry + col_offset, PREFERRED_VECTOR_BYTE_SIZE);
		
		strip_start_bit = make_bit_on;
		or_of_result = GoLGrid_int_evolve_strip (in_entry, in_entry_right, out_entry, out_entry_right, strip_start_bit & 0x3f, make_row_cnt);
	}
	
	s32 offset_leftmost_nonempty = strip_start_bit;
	s32 offset_rightmost_nonempty = strip_start_bit;
	s32 next_strip_start_bit = strip_start_bit + 62;
	
	// It's been proven that this filtering step cannot be omitted - this is an example pattern that would give the wrong out population limits without it:
	// x = 256, y = 256, rule = LifeHistory
	// 128$63.A60.A61.A$123.A$123.2A$123.2A$123.2A.A$123.2A$123.2A$123.A$124.A!
	if (next_strip_start_bit < make_bit_off - 2)
		or_of_result &= 0xfffffffffffffffeu;
	
	u64 leftmost_nonempty_or_of_result = or_of_result;
	u64 rightmost_nonempty_or_of_result = or_of_result;
	
	// Loop to generate the remaining strips in the evolved grid
	while (next_strip_start_bit < make_bit_off - 2)
	{
		in_entry = in_gg->grid + (col_offset * (u64) (next_strip_start_bit >> 6)) + (u64) make_row_on;
		out_entry = out_gg->grid + (col_offset * (u64) (next_strip_start_bit >> 6)) + (u64) make_row_on;
		
		in_entry = align_down_const_pointer (in_entry, PREFERRED_VECTOR_BYTE_SIZE);
		out_entry = align_down_pointer (out_entry, PREFERRED_VECTOR_BYTE_SIZE);
		
		if ((next_strip_start_bit & 0x3f) == 0 || next_strip_start_bit > ((make_col_off - 1) << 6))
		{
			strip_start_bit = next_strip_start_bit & 0xffffffc0;
			or_of_result = GoLGrid_int_evolve_column_merge (in_entry, out_entry, make_row_cnt);
		}
		else
		{
			const u64 *in_entry_right = align_down_const_pointer (in_entry + col_offset, PREFERRED_VECTOR_BYTE_SIZE);
			u64 *out_entry_right = align_down_pointer (out_entry + col_offset, PREFERRED_VECTOR_BYTE_SIZE);
			
			strip_start_bit = next_strip_start_bit;
			or_of_result = GoLGrid_int_evolve_strip_merge (in_entry, in_entry_right, out_entry, out_entry_right, strip_start_bit & 0x3f, make_row_cnt);
		}
		
		next_strip_start_bit = strip_start_bit + 62;
		
		if (next_strip_start_bit < make_bit_off - 2)
			or_of_result &= 0xfffffffffffffffeu;
		
		if (or_of_result != 0)
		{
			if (leftmost_nonempty_or_of_result == 0)
			{
				leftmost_nonempty_or_of_result = or_of_result;
				offset_leftmost_nonempty = strip_start_bit;
			}
			rightmost_nonempty_or_of_result = or_of_result;
			offset_rightmost_nonempty = strip_start_bit;
		}
	}
	
	// Find the population limits of the output grid
	if (leftmost_nonempty_or_of_result == 0)
		GoLGrid_int_set_empty_population_rect (out_gg);
	else
	{
		out_gg->pop_x_on = offset_leftmost_nonempty + (63 - most_significant_bit_u64 (leftmost_nonempty_or_of_result));
		out_gg->pop_x_off = offset_rightmost_nonempty + (64 - least_significant_bit_u64 (rightmost_nonempty_or_of_result));
		
		out_gg->pop_y_on = required_row_on;
		out_gg->pop_y_off = required_row_off;
		GoLGrid_int_tighten_pop_y_on (out_gg);
		GoLGrid_int_tighten_pop_y_off (out_gg);
	}
}

static __noinline void GoLGrid_evolve__noinline (const GoLGrid *in_gg, GoLGrid *out_gg)
{
	GoLGrid_evolve (in_gg, out_gg);
}
