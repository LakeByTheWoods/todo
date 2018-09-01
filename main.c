
#include <sys/types.h>
#include <dirent.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

/* ncurses */
#include <curses.h>

#define ARRAY_COUNT(a) (sizeof(a)/sizeof(*a))

enum Todo_State
{
	State_Not_Started,
	State_Priority,
	State_Doing,
	State_Done,
};

struct Todo_List
{
	struct Todo_List *next;
	time_t time_added;
	time_t time_started;
	time_t time_complete;
	enum Todo_State state;
	char text[];
};

struct Todo_List_View
{
	struct Todo_List *listing;
	int selection_index;
	int scrolling;
};

static void todo_list_free(struct Todo_List *listing)
{
	while (listing)
	{
		struct Todo_List *to_free = listing;
		listing = listing->next;
		free(to_free);
	}
}

static int todo_list_count(struct Todo_List *listing)
{
	int count = 0;
	while (listing)
	{
		count++;
		listing = listing->next;
	}
	return count;
}

static void draw_todo_list_view_to_window(WINDOW *window, struct Todo_List_View *lview)
{
	int draw_x = 0,
	    draw_y = 0,
	    list_index = 0;
	struct Todo_List *listing = lview->listing;
	while (listing)
	{
		if (list_index >= lview->scrolling)
		{
			if (lview->selection_index == list_index) attron(A_STANDOUT);
			mvwaddstr(stdscr, draw_y, draw_x, listing->text);
			if (lview->selection_index == list_index) attroff(A_STANDOUT);
			++draw_y;
		}
		listing = listing->next;
		++list_index;
	}
}

int main(int argc, char **argv)
{
	initscr();   // start up ncurses
	cbreak();    // don't wait for return when getting chars
	noecho();    // don't echo keypresses
	nonl();      // we wan't to be able to detect the return key
	clear();     // make sure screen is cleared because some implementations don't do it automatically
	curs_set(0); // set cursor invisible

	intrflush(stdscr, FALSE);
	keypad(stdscr, TRUE); // get special keys
	idlok(stdscr, true);
	leaveok(stdscr, true);
	scrollok(stdscr, true); // enable window scrolling

	{
		{
			struct Todo_List_View *lview = calloc(1, sizeof(*lview));
			lview->listing = 0; // TODO: Load listing

			{
				int window_width, window_height;
				int ch = 0;
				(void)window_width;
				do
				{
					getmaxyx(stdscr, window_height, window_width);
					if (ch == KEY_UP)
					{
						--lview->selection_index;
						if (lview->selection_index < lview->scrolling)
						{
							if (lview->scrolling == 0)
							{
								lview->selection_index = 0;
							}
							else
							{
								--lview->scrolling;
							}
						}
					}
					if (ch == KEY_DOWN)
					{
						++lview->selection_index;
						if (lview->selection_index >= todo_list_count(lview->listing))
						{
							lview->selection_index = todo_list_count(lview->listing) - 1;
						}
						if (lview->selection_index - lview->scrolling >= window_height)
						{
							++lview->scrolling;
						}
					}
					if (ch == KEY_LEFT)
					{
						lview->listing[lview->selection_index].state = State_Not_Started;
					}
					if (ch == KEY_RIGHT)
					{
						switch (lview->listing[lview->selection_index].state)
						{
							case State_Not_Started:
							case State_Priority:
								lview->listing[lview->selection_index].state = State_Doing;
								break;

							case State_Doing:
								lview->listing[lview->selection_index].state = State_Done;
								break;

							case State_Done:
								break;
						}
					}
					werase(stdscr);
					wrefresh(stdscr);
					draw_todo_list_view_to_window(stdscr, lview);
				} while (ch = getch(), ch != 'q');
				todo_list_free(lview->listing);
				free(lview);
			}
		}
	}

	endwin();
	return 0;
}
