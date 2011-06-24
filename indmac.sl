% Jed C-Mode enhancement for indenting multi-line macros
% Still slightly buggy, but it works well enough for me..
%  - dark@mama.indstate.edu -

define indmac ()
{
    variable numlines;

    push_spot();
    EXIT_BLOCK
    {
	pop_spot();
    }
    bol();
    !if (looking_at("#define"))
      return;
    eol();
    go_left_1();
    !if(looking_at_char('\\'))
      return;
    eol();
    insert(" \n{");
    go_down_1();
    eol();
    go_left_1();
    numlines = 0;
    while(looking_at_char('\\')) {
	++numlines;
	c_indent_line();
	eol();
	go_left_1();
	del();
	trim();
	go_down_1();
	eol();
	go_left_1();
    }
    pop_spot();
    push_spot();
    eol(); trim();
    go_down_1();
    bol();
    del_through_eol();
    go_up_1();
    loop(numlines) {
	go_down_1();
	eol();
	insert(" \\");
    }
}

define do_c_indent()
{
    push_spot();
    while(up_1()) {
	eol(); trim();
	!if(bolp())
	  go_left_1();
	!if(looking_at_char('\\')) {
	    go_down_1();
	    break;
	}
    }
    bol();
    if (looking_at("#define")) {
	indmac();
	pop_spot();
	push_spot();
	eol();
	go_left_1();
	while(looking_at_char('\\')) {
	    go_down_1();
	    eol();
	    go_left_1();
	}
	bol_skip_white();
	pop_spot();
    } else {
	pop_spot();
	c_indent_line();
    }
}

define do_c_indent_nl()
{
    variable did_up = 0;

    push_spot();
    do {
	eol(); trim();
	!if(bolp())
	  go_left_1();
	!if(looking_at_char('\\')) {
	    if(did_up)
	      go_down_1();
	    break;
	}
	did_up = 1;
    } while(up_1());
    bol();
    if (looking_at("#define")) {
	push_mark();
	pop_spot();
	newline();
	push_spot();
	insert(";\\");
	pop_mark_1();
	indmac();
	pop_spot();
	skip_white();
	del();
    } else {
	pop_spot();
	c_newline_and_indent();
    }
}

define c_mode_hook ()
{
    set_buffer_hook("indent_hook", "do_c_indent");
    set_buffer_hook("newline_indent_hook", "do_c_indent_nl");
}
