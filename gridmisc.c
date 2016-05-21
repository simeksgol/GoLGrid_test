static __noinline int GoLGrid_or_text_pattern (GoLGrid *gg, const char *pattern, int left_x, int top_y)
{
	int not_clipped = TRUE;
	s32 text_ix = 0;
	s32 cur_y = top_y;
	s32 cur_x = left_x;
	
	while (TRUE)
	{
		char c = pattern [text_ix++];
		
		if (c == '\0')
			break;
		else if (c == '\n')
		{
			cur_y++;
			cur_x = left_x;
		}
		else
		{
			if (c == ' ' || c == '.')
				; // Do nothing
			else if (c == '*' || c == '@')
				not_clipped &= GoLGrid_set_cell_on (gg, cur_x, cur_y);
			else
				fprintf (stderr, "Illegal character in %s\n", __func__);
			
			cur_x++;
		}
	}
	
	return not_clipped;
}

static void GoLGrid_print (const GoLGrid *gg)
{
	if (!gg || !gg->grid)
		return (void) ffsc (__func__);
	
	if (GoLGrid_is_empty (gg))
	{
		printf ("--- Empty grid\n\n");
		return;
	}
	
	Rect pr;
	GoLGrid_get_bounding_box (gg, &pr);
	Rect_add_borders (&pr, 2);
	
	int y;
	int x;
	for (y = pr.top_y; y < pr.top_y + pr.height; y++)
	{
		for (x = pr.left_x; x < pr.left_x + pr.width; x++)
			printf ("%c", (GoLGrid_get_cell (gg, x, y) != 0) ? '@' : '.');
		
		printf ("\n");
	}
	
	printf ("\n");
}

static __noinline void GoLGrid_int_print_life_history_symbol (FILE *stream, char symbol, s32 count, int *line_length)
{
	if (!stream || !line_length)
		return (void) ffsc (__func__);
	
	if (count == 0)
		return;
	else if (count == 1)
	{
		fprintf (stream, "%c", symbol);
		(*line_length)++;
	}
	else if (count > 1)
	{
		fprintf (stream, "%d%c", count, symbol);
		(*line_length) += (1 + digits_in_u32 ((u32) count));
	}
	
	if (*line_length > 68)
	{
		fprintf (stream, "\n");
		(*line_length) = 0;
	}
}

static __noinline void GoLGrid_print_life_history_full (FILE *stream, const Rect *print_rect, const GoLGrid *on_gg, const GoLGrid *marked_gg, const GoLGrid *envelope_gg, const GoLGrid *special_gg)
{
	if ((print_rect != NULL && (print_rect->width < 0 || print_rect->height < 0)) || (!on_gg && !marked_gg && !envelope_gg && !special_gg) ||
			(on_gg && !on_gg->grid) || (marked_gg && !marked_gg->grid) || (envelope_gg && !envelope_gg->grid) || (special_gg && !special_gg->grid))
		return (void) ffsc (__func__);
	
	if (!stream)
		stream = stdout;
	
	const Rect *pr = (print_rect ? print_rect : &on_gg->grid_rect);
	
// FIXME: What if the grid rects are different?
	
	fprintf (stream, "x = %d, y = %d, rule = LifeHistory\n", pr->width, pr->height);
	
	int line_length = 0;
	int unwritten_cell_state = 0;
	s32 unwritten_cell_count = 0;
	s32 unwritten_newline_count = 0;
	
	s32 y;
	s32 x;
	for (y = pr->top_y; y < pr->top_y + pr->height; y++)
	{
		for (x = pr->left_x; x < pr->left_x + pr->width; x++)
		{
			int cell_state = 0;
			
			if (on_gg && GoLGrid_get_cell (on_gg, x, y))
				cell_state = 1;
			
			if (marked_gg && GoLGrid_get_cell (marked_gg, x, y))
				cell_state = ((cell_state == 1) ? 3 : 4);
			
			if (special_gg && GoLGrid_get_cell (special_gg, x, y))
				cell_state = ((cell_state == 1 || cell_state == 3) ? 5 : 6);
			
			if (envelope_gg && cell_state == 0 && GoLGrid_get_cell (envelope_gg, x, y))
				cell_state = 2;
			
			if (unwritten_newline_count > 0 && cell_state != 0)
			{
				GoLGrid_int_print_life_history_symbol (stream, '$', unwritten_newline_count, &line_length);
				unwritten_newline_count = 0;
			}
			
			if (unwritten_cell_count > 0 && cell_state != unwritten_cell_state)
			{
				char symbol = (unwritten_cell_state == 0 ? '.' : 'A' + (unwritten_cell_state - 1));
				GoLGrid_int_print_life_history_symbol (stream, symbol, unwritten_cell_count, &line_length);
				
				unwritten_cell_count = 0;
			}
			
			unwritten_cell_state = cell_state;
			unwritten_cell_count++;
		}
		
		if (unwritten_cell_count > 0 && unwritten_cell_state != 0)
			GoLGrid_int_print_life_history_symbol (stream, 'A' + (unwritten_cell_state - 1), unwritten_cell_count, &line_length);
		
		unwritten_cell_count = 0;
		unwritten_newline_count++;
	}
	
	fprintf (stream, "!\n");
}

static __noinline void GoLGrid_print_life_history (const GoLGrid *on_gg)
{
	GoLGrid_print_life_history_full (stdout, NULL, on_gg, NULL, NULL, NULL);
}

static __noinline int GoLGrid_int_get_life_history_symbol (const char **lh, int *success, int *state, s32 *count)
{
	if (success)
		*success = FALSE;
	if (state)
		*state = 0;
	if (count)
		*count = 0;
	
	if (!lh || !*lh || !success || !state || !count)
		return ffsc (__func__);
	
	while (TRUE)
	{
		char c = **lh;
		
		if (c == '!' || c == '\0')
		{
			*success = TRUE;
			return FALSE;
		}
		
		if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
		{
			(*lh)++;
			continue;
		}
		
		s32 cnt = 1;
		if (c >= '0' && c <= '9')
		{
			u64 cnt_u64;
			if (!parse_u64 (lh, &cnt_u64) || cnt_u64 > s32_MAX)
				return FALSE;
			
			cnt = cnt_u64;
			c = **lh;
		}
		
		if (c == '$')
			*state = -1;
		else if (c == '.' || c == 'b')
			*state = 0;
		else if (c == 'o')
			*state = 1;
		else if (c >= 'A' && c <= 'F')
			*state = 1 + (c - 'A');
		else
			return FALSE;
		
		(*lh)++;
		*success = TRUE;
		*count = cnt;
		return TRUE;
	}
}

static __noinline int GoLGrid_parse_life_history (const char *lh, s32 left_x, s32 top_y, GoLGrid *on_gg, GoLGrid *marked_gg, GoLGrid *envelope_gg, GoLGrid *special_gg, int *clipped, int *reinterpreted)
{
	if (clipped)
		*clipped = FALSE;
	
	if (reinterpreted)
		*reinterpreted = FALSE;
	
	if (!lh || (!on_gg && !marked_gg && !envelope_gg && !special_gg) || (on_gg && !on_gg->grid) || (marked_gg && !marked_gg->grid) || (envelope_gg && !envelope_gg->grid) || (special_gg && !special_gg->grid))
		return ffsc (__func__);
	
	if (on_gg)
		GoLGrid_clear (on_gg);
	if (marked_gg)
		GoLGrid_clear (marked_gg);
	if (envelope_gg)
		GoLGrid_clear (envelope_gg);
	if (special_gg)
		GoLGrid_clear (special_gg);
	
	int used_state [7];
	int state_ix;
	for (state_ix = 0; state_ix <= 6; state_ix++)
		used_state [state_ix] = FALSE;
	
	s32 cur_x = left_x;
	s32 cur_y = top_y;
	
	int overflow_x = FALSE;
	int overflow_y = FALSE;
	int not_clipped = TRUE;
	int success;
	
	while (TRUE)
	{
		int state;
		s32 count;
		
		if (!GoLGrid_int_get_life_history_symbol (&lh, &success, &state, &count))
			break;
		
		if (count == 0)
			continue;
		
		if (state == -1)
		{
			cur_x = left_x;
			overflow_x = FALSE;
			
			s32 old_y = cur_y;
			cur_y += count;
			if (cur_y < old_y)
				overflow_y = TRUE;
		}
		else if (state == 0)
		{
			s32 old_x = cur_x;
			cur_x += count;
			if (cur_x < old_x)
				overflow_x = TRUE;
		}
		else
		{
			s32 count_ix;
			for (count_ix = 0; count_ix < count; count_ix++)
			{
				if (overflow_y || overflow_x)
				{
					not_clipped = FALSE;
					break;
				}
				else
				{
					used_state [state] = TRUE;
					
					if (on_gg && (state == 1 || state == 3 || state == 5))
						not_clipped &= GoLGrid_set_cell_on (on_gg, cur_x, cur_y);
					
					if (marked_gg && (state == 3 || state == 4 || state == 5))
						not_clipped &= GoLGrid_set_cell_on (marked_gg, cur_x, cur_y);
					
					if (envelope_gg && (state == 2))
						not_clipped &= GoLGrid_set_cell_on (envelope_gg, cur_x, cur_y);
					
					if (special_gg && (state == 5 || state == 6))
						not_clipped &= GoLGrid_set_cell_on (special_gg, cur_x, cur_y);
				}
				
				cur_x++;
				if (cur_x <= left_x)
					overflow_x = TRUE;
			}
		}
	}
	
	if (!success)
	{
		*clipped = FALSE;
		
		if (on_gg)
			GoLGrid_clear (on_gg);
		if (marked_gg)
			GoLGrid_clear (marked_gg);
		if (envelope_gg)
			GoLGrid_clear (envelope_gg);
		if (special_gg)
			GoLGrid_clear (special_gg);
		
		return FALSE;
	}
	
	if (clipped)
		*clipped = !not_clipped;
	
	if (reinterpreted)
	{
		if (!on_gg && (used_state [1] || used_state [3] || used_state [5]))
			*reinterpreted = TRUE;
		if (!marked_gg && (used_state [3] || used_state [4] || (!special_gg && used_state [3] && used_state [5])))
			*reinterpreted = TRUE;
		if (!envelope_gg && used_state [2])
			*reinterpreted = TRUE;
		if (!special_gg && used_state [6])
			*reinterpreted = TRUE;
	}
	
	return TRUE;
}
