static __noinline void GoLGrid_debug_compare (const GoLGrid *obj_gg, const GoLGrid *ref_gg)
{
	if (!obj_gg || !obj_gg->grid || !ref_gg || !ref_gg->grid)
		return (void) ffsc (__func__);
	
	if (!Rect_is_equal (&obj_gg->grid_rect, &ref_gg->grid_rect))
	{
		fprintf (stderr, "Compare error, different grid rectangles\n");
		exit (0);
	}
	if (obj_gg->pop_x_on != ref_gg->pop_x_on || obj_gg->pop_x_off != ref_gg->pop_x_off || obj_gg->pop_y_on != ref_gg->pop_y_on || obj_gg->pop_y_off != ref_gg->pop_y_off)
	{
		fprintf (stderr, "Compare error generation %d, population limits don't match\n", (int) obj_gg->generation);
		fprintf (stderr, "Object:    x = (%d, %d), y = (%d, %d)\n", obj_gg->pop_x_on, obj_gg->pop_x_off, obj_gg->pop_y_on, obj_gg->pop_y_off);
		fprintf (stderr, "Reference: x = (%d, %d), y = (%d, %d)\n", ref_gg->pop_x_on, ref_gg->pop_x_off, ref_gg->pop_y_on, ref_gg->pop_y_off);
		GoLGrid_print_life_history (obj_gg);
		GoLGrid_print_life_history (ref_gg);
		exit (0);
	}
	
	s32 row_cnt = obj_gg->grid_rect.height;
	s32 col_cnt = (obj_gg->grid_rect.width + 63) >> 6;
	
	s32 col_ix;
	s32 row_ix;
	for (col_ix = col_cnt - 1; col_ix >= 0; col_ix--)
		for (row_ix = -1; row_ix < (row_cnt + 1); row_ix++)
			if (obj_gg->grid [(obj_gg->col_offset * col_ix) + row_ix] != ref_gg->grid [(ref_gg->col_offset * col_ix) + row_ix])
			{
				fprintf (stderr, "Compare error, grid contents mismatch\n");
				exit (0);
			}
	
	if (obj_gg->generation != ref_gg->generation)
	{
		fprintf (stderr, "Compare error, generation count mismatch\n");
		exit (0);
	}
}

static __noinline s32 GoLGrid_debug_search_pop_x_on (const GoLGrid *gg)
{
	if (!gg || !gg->grid)
		return ffsc (__func__);
	
	s32 row_cnt = gg->grid_rect.height;
	s32 col_cnt = (gg->grid_rect.width + 63) >> 6;
	
	s32 col_ix;
	s32 row_ix;
	for (col_ix = 0; col_ix < col_cnt; col_ix++)
	{
		u64 or_of_col = 0;
		for (row_ix = 0; row_ix < row_cnt; row_ix++)
			or_of_col |= gg->grid [(gg->col_offset * col_ix) + row_ix];
		
		if (or_of_col != 0)
			return (64 * col_ix) + (63 - most_significant_bit_u64 (or_of_col));
	}
	
	return -1;
}

static __noinline s32 GoLGrid_debug_search_pop_x_off (const GoLGrid *gg)
{
	if (!gg || !gg->grid)
		return ffsc (__func__);
	
	s32 row_cnt = gg->grid_rect.height;
	s32 col_cnt = (gg->grid_rect.width + 63) >> 6;
	
	s32 col_ix;
	s32 row_ix;
	for (col_ix = col_cnt - 1; col_ix >= 0; col_ix--)
	{
		u64 or_of_col = 0;
		for (row_ix = 0; row_ix < row_cnt; row_ix++)
			or_of_col |= gg->grid [(gg->col_offset * col_ix) + row_ix];
		
		if (or_of_col != 0)
			return (64 * col_ix) + (64 - least_significant_bit_u64 (or_of_col));
	}
	
	return -1;
}

static __noinline s32 GoLGrid_debug_search_pop_y_on (const GoLGrid *gg)
{
	if (!gg || !gg->grid)
		return ffsc (__func__);
	
	s32 row_cnt = gg->grid_rect.height;
	s32 col_cnt = (gg->grid_rect.width + 63) >> 6;
	
	s32 row_ix;
	s32 col_ix;
	for (row_ix = 0; row_ix < row_cnt; row_ix++)
		for (col_ix = 0; col_ix < col_cnt; col_ix++)
			if (gg->grid [(gg->col_offset * col_ix) + row_ix] != 0)
				return row_ix;
	
	return -1;
}

static __noinline s32 GoLGrid_debug_search_pop_y_off (const GoLGrid *gg)
{
	if (!gg || !gg->grid)
		return ffsc (__func__);
	
	s32 row_cnt = gg->grid_rect.height;
	s32 col_cnt = (gg->grid_rect.width + 63) >> 6;
	
	s32 row_ix;
	s32 col_ix;
	for (row_ix = row_cnt - 1; row_ix >= 0; row_ix--)
		for (col_ix = 0; col_ix < col_cnt; col_ix++)
			if (gg->grid [(gg->col_offset * col_ix) + row_ix] != 0)
				return row_ix + 1;
	
	return -1;
}

static __noinline void GoLGrid_debug_verify_consistency (const GoLGrid *gg)
{
	if (!gg || !gg->grid)
		return (void) ffsc (__func__);
	
	s32 row_cnt = gg->grid_rect.height;
	s32 col_cnt = (gg->grid_rect.width + 63) >> 6;
	
	s32 col_ix;
	s32 row_delta;
	for (col_ix = 0; col_ix < col_cnt; col_ix++)
		for (row_delta = 1; row_delta <= (s32) (PREFERRED_VECTOR_BYTE_SIZE / sizeof (u64)); row_delta++)
		{
			if (gg->grid [(gg->col_offset * col_ix) - row_delta] != 0)
			{
				fprintf (stderr, "Consistency fail generation %d, read-only rows above grid were non-empty\n", (int) gg->generation);
				exit (0);
			}
			if (gg->grid [(gg->col_offset * col_ix) + ((row_cnt - 1) + row_delta)] != 0)
			{
				fprintf (stderr, "Consistency fail generation %d, read-only rows below grid were non-empty\n", (int) gg->generation);
				exit (0);
			}
		}
	
	s32 pop_x_on = GoLGrid_debug_search_pop_x_on (gg);
	s32 pop_x_off = GoLGrid_debug_search_pop_x_off (gg);
	s32 pop_y_on = GoLGrid_debug_search_pop_y_on (gg);
	s32 pop_y_off = GoLGrid_debug_search_pop_y_off (gg);
	
	if (pop_x_on == -1)
	{
		if (gg->pop_x_on != gg->grid_rect.width / 2 || gg->pop_x_off != gg->grid_rect.width / 2 || gg->pop_y_on != gg->grid_rect.height / 2 || gg->pop_y_off != gg->grid_rect.height / 2)
		{
			fprintf (stderr, "Consistency fail generation %d, population limits not correct for empty grid\n", (int) gg->generation);
			exit (0);
		}
		else
			return;
	}
			
	if (gg->pop_x_on != pop_x_on || gg->pop_x_off != pop_x_off || gg->pop_y_on != pop_y_on || gg->pop_y_off != pop_y_off)
	{
		fprintf (stderr, "Consistency fail generation %d, population limits don't match grid contents\n", (int) gg->generation);
		fprintf (stderr, "Stored:     x = (%d, %d), y = (%d, %d)\n", gg->pop_x_on, gg->pop_x_off, gg->pop_y_on, gg->pop_y_off);
		fprintf (stderr, "Calculated: x = (%d, %d), y = (%d, %d)\n", pop_x_on, pop_x_off, pop_y_on, pop_y_off);
		GoLGrid_print_life_history (gg);
		exit (0);
	}
}

static __noinline void GoLGrid_debug_clear_entire_grid (GoLGrid *gg)
{
	if (!gg || !gg->grid)
		return (void) ffsc (__func__);
	
	s32 row_cnt = gg->grid_rect.height;
	s32 col_cnt = (gg->grid_rect.width + 63) >> 6;
	
	s32 col_ix;
	s32 row_ix;
	for (col_ix = 0; col_ix < col_cnt; col_ix++)
		for (row_ix = -(s32) (PREFERRED_VECTOR_BYTE_SIZE / sizeof (u64)); row_ix < row_cnt + (s32) (PREFERRED_VECTOR_BYTE_SIZE / sizeof (u64)); row_ix++)
			gg->grid [(gg->col_offset * col_ix) + row_ix] = 0;
	
	GoLGrid_int_set_empty_population_rect (gg);
	gg->generation = 0;
}

static __noinline void GoLGrid_debug_evolve_column (const u64 *in_entry, u64 *out_entry, s32 row_count)
{
	if (!in_entry || !out_entry || row_count <= 0)
		return (void) ffsc (__func__);
	
	s32 row_ix;
	for (row_ix = 0; row_ix < row_count; row_ix++)
	{
		*out_entry++ = GoLGrid_int_evolve_word (in_entry [-1], in_entry [0], in_entry [1]);
		in_entry++;
	}
}

static __noinline void GoLGrid_debug_evolve_between_columns (const u64 *in_entry_left, const u64 *in_entry_right, u64 *out_entry_left, u64 *out_entry_right, s32 row_count)
{
	if (!in_entry_left || !in_entry_right || !out_entry_left || !out_entry_right || row_count <= 0)
		return (void) ffsc (__func__);
	
	s32 row_ix;
	for (row_ix = 0; row_ix < row_count; row_ix++)
	{
		u64 upper_word = (in_entry_left [-1] << 32) | (in_entry_right [-1] >> 32);
		u64 mid_word = (in_entry_left [0] << 32) | (in_entry_right [0] >> 32);
		u64 lower_word = (in_entry_left [1] << 32) | (in_entry_right [1] >> 32);
		in_entry_left++;
		in_entry_right++;
		
		u64 out_word = GoLGrid_int_evolve_word (upper_word, mid_word, lower_word) & 0x0000000180000000u;
		
		out_entry_left [0] = (out_entry_left [0] & 0xfffffffffffffffeu) | (out_word >> 32);
		out_entry_right [0] = (out_entry_right [0] & 0x7fffffffffffffffu) | (out_word << 32);
		out_entry_left++;
		out_entry_right++;
	}
}

static __noinline void GoLGrid_debug_set_population_limits_from_scratch (GoLGrid *gg)
{
	s32 pop_x_on = GoLGrid_debug_search_pop_x_on (gg);
	s32 pop_x_off = GoLGrid_debug_search_pop_x_off (gg);
	s32 pop_y_on = GoLGrid_debug_search_pop_y_on (gg);
	s32 pop_y_off = GoLGrid_debug_search_pop_y_off (gg);
	
	if (pop_x_on == -1)
		GoLGrid_int_set_empty_population_rect (gg);
	else
	{
		gg->pop_x_on = pop_x_on;
		gg->pop_x_off = pop_x_off;
		gg->pop_y_on = pop_y_on;
		gg->pop_y_off = pop_y_off;
	}
}

static __noinline void GoLGrid_debug_reference_evolve (const GoLGrid *in_gg, GoLGrid *out_gg)
{
	if (!in_gg || !in_gg->grid || !out_gg || !out_gg->grid || out_gg->grid_rect.width != in_gg->grid_rect.width || out_gg->grid_rect.height != in_gg->grid_rect.height)
		return (void) ffsc (__func__);
	
	out_gg->grid_rect.left_x = in_gg->grid_rect.left_x;
	out_gg->grid_rect.top_y = in_gg->grid_rect.top_y;
	out_gg->generation = in_gg->generation + 1;
	
	s32 row_cnt = in_gg->grid_rect.height;
	s32 col_cnt = (in_gg->grid_rect.width + 63) >> 6;
	
	GoLGrid_debug_clear_entire_grid (out_gg);
	
	s32 col_ix;
	for (col_ix = 0; col_ix < col_cnt; col_ix++)
		GoLGrid_debug_evolve_column (in_gg->grid + (col_ix * in_gg->col_offset), out_gg->grid + (col_ix * out_gg->col_offset), row_cnt);
	
	for (col_ix = 0; col_ix < (col_cnt - 1); col_ix++)
		GoLGrid_debug_evolve_between_columns (in_gg->grid + (col_ix * in_gg->col_offset), in_gg->grid + ((col_ix + 1) * in_gg->col_offset),
				out_gg->grid + (col_ix * out_gg->col_offset), out_gg->grid + ((col_ix + 1) * out_gg->col_offset), row_cnt);
	
	GoLGrid_debug_set_population_limits_from_scratch (out_gg);
}

// A pattern that stays within a 60-by-60 box from an 8-by-8 starting pattern in the center of it, for 1181 generations
static __noinline void GoLGrid_debug_or_slow_expansion_test_pattern (GoLGrid *gg, int left_x, int top_y)
{
	if (!gg || !gg->grid)
		return (void) ffsc (__func__);
	
	int y;
	int x;
	for (y = 0; y < 8; y++)
		for (x = 0; x < 8; x++)
			if (0x53c7639c0bdf9d69u & (((u64) 1) << (8 * y + x)))
				GoLGrid_set_cell_on (gg, left_x + x, top_y + y);
}
