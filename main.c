
#define XOPEN_SOURCE_EXTENDED

#include <sys/types.h>
#include <dirent.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <locale.h>
#include <wchar.h>

/* ncurses */
#include <ncurses.h>

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
    wchar_t *text;
};

struct Todo_List *todo_list_load(char *file_path)
{
    int fd = open(file_path, O_RDONLY);
    assert(fd != -1);

    off_t offset_end = lseek(fd, 0, SEEK_END);
    assert(offset_end != -1);
    lseek(fd, 0, SEEK_SET);

    char *file_contents = calloc(offset_end + 1, 1);
    read(fd, file_contents, offset_end);

    struct Todo_List *result;
    struct Todo_List **next = &result;

    wchar_t *start_ptr = calloc(offset_end + 1, sizeof (wchar_t));
    swprintf(start_ptr, offset_end + 1, L"%s", file_contents);
    wchar_t *save_ptr = NULL;
    do
    {
        wchar_t *line = wcstok(start_ptr, L"\n", &save_ptr);
        start_ptr = NULL;
        if (! line) break;

        time_t time_added = wcstoll(line, &line, 16);
        time_t time_started = wcstoll(line, &line, 16);
        time_t time_complete = wcstoll(line, &line, 16);
        long long statell = wcstoll(line, &line, 10);

        enum Todo_State state = (enum Todo_State)statell;

        *next = calloc(1, sizeof (struct Todo_List));
        **next = 
            (struct Todo_List)
            {
                .time_added = time_added,
                .time_started = time_started,
                .time_complete = time_complete,
                .state = state,
                .text = wcsdup(line),
            };
        next = &(*next)->next;
    } while (1);

    free(file_contents);
    close(fd);

    return result;
}

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

static _Bool s_enable_unicode = false;

static void draw_todo_list_view_to_window(WINDOW *window, struct Todo_List_View *lview)
{
    int draw_x = 0,
        draw_y = 0,
        list_index = 0;

    int max_x, max_y; (void)max_y;
    getmaxyx(window, max_y, max_x);
    wchar_t *space = calloc(max_x, sizeof (*space));
    for (int i = 0; i < max_x; ++i)
    {
        wcscat(space, L" ");
    }
    
    struct Todo_List *listing = lview->listing;
    while (listing)
    {
#define IS_SELECTED (lview->selection_index == list_index)
        if (list_index >= lview->scrolling)
        {
            if (IS_SELECTED) wattron(window, WA_STANDOUT);

            {
                // Clear whole line
                mvwaddnwstr(window, draw_y, 0, space, max_x);
            }

            if (IS_SELECTED)
            {
                mvwaddnwstr(window, draw_y, draw_x, s_enable_unicode ? L"\uF00C    " : L" >  ", 4);
            }
            else
            {
                mvwaddnwstr(window, draw_y, draw_x, L"    ", 4);
            }
            draw_x += 4;


            wchar_t scratch_buffer[64];
            switch (listing->state)
            {
                case State_Not_Started:
                {
                    size_t date_string_length = wcsftime(scratch_buffer,
                                                         ARRAY_COUNT(scratch_buffer),
                                                         L"%y-%m %a ",
                                                         gmtime(&listing->time_added));
                    mvwaddwstr(window, draw_y, draw_x, scratch_buffer);
                    draw_x += date_string_length;
                    wattron(window, COLOR_PAIR(1));
                    mvwaddnwstr(window, draw_y, draw_x, s_enable_unicode ? L"\uF00C  " : L" . ", 3);
                    wattroff(window, COLOR_PAIR(1));
                    break;
                }
                case State_Priority:
                {
                    size_t date_string_length = wcsftime(scratch_buffer,
                                                         ARRAY_COUNT(scratch_buffer),
                                                         L"%y-%m %a ",
                                                         gmtime(&listing->time_added));
                    mvwaddwstr(window, draw_y, draw_x, scratch_buffer);
                    draw_x += date_string_length;
                    wattron(window, COLOR_PAIR(2));
                    mvwaddnwstr(window, draw_y, draw_x, s_enable_unicode ? L"\uF00C  " : L" ! ", 3);
                    wattroff(window, COLOR_PAIR(2));
                    break;
                }
                case State_Doing:
                {
                    size_t date_string_length = wcsftime(scratch_buffer,
                                                         ARRAY_COUNT(scratch_buffer),
                                                         L"%y-%m %a ",
                                                         gmtime(&listing->time_added));
                    mvwaddwstr(window, draw_y, draw_x, scratch_buffer);
                    draw_x += date_string_length;
                    wattron(window, COLOR_PAIR(4));
                    mvwaddnwstr(window, draw_y, draw_x, s_enable_unicode ? L"\uF00C  " : L" O ", 3);
                    wattroff(window, COLOR_PAIR(4));
                    break;
                }
                case State_Done:
                {
                    size_t date_string_length = wcsftime(scratch_buffer,
                                                         ARRAY_COUNT(scratch_buffer),
                                                         L"%y-%m %a ",
                                                         gmtime(&listing->time_added));
                    mvwaddwstr(window, draw_y, draw_x, scratch_buffer);
                    draw_x += date_string_length;
                    wattron(window, COLOR_PAIR(5));
                    mvwaddnwstr(window, draw_y, draw_x, s_enable_unicode ? L"\uF00C  " : L" X ", 3);
                    wattroff(window, COLOR_PAIR(5));
                    break;
                }
            }

            draw_x += 3;

            //mvwaddwstr(window, draw_y, draw_x, L"Hello, \uF00C World!");
            mvwaddwstr(window, draw_y, draw_x, listing->text);
            if (lview->selection_index == list_index) wattroff(window, WA_STANDOUT);
            ++draw_y;
            draw_x = 0;
        }
#undef IS_SELECTED
#undef SHRINK_ON_SELECTION
        listing = listing->next;
        ++list_index;
    }

    free (space);
}

int main(int argc, char **argv)
{
    setlocale(LC_ALL, "");
    initscr();   // start up ncurses
    cbreak();    // don't wait for return when getting chars
    noecho();    // don't echo keypresses
    nonl();      // we wan't to be able to detect the return key
    clear();     // make sure screen is cleared because some implementations don't do it automatically
    curs_set(0); // set cursor invisible
    start_color();
    init_pair(1, COLOR_WHITE, COLOR_BLACK);
    init_pair(2, COLOR_RED, COLOR_BLACK);
    init_pair(4, COLOR_CYAN, COLOR_BLACK);
    init_pair(5, COLOR_GREEN, COLOR_BLACK);

    intrflush(stdscr, FALSE);
    keypad(stdscr, TRUE); // get special keys
    idlok(stdscr, true);
    leaveok(stdscr, true);
    scrollok(stdscr, true); // enable window scrolling
    wtimeout(stdscr, 100);

    {
        struct Todo_List_View *lview = calloc(1, sizeof(*lview));
        lview->listing = todo_list_load("todolist");

        {
            int window_width, window_height;
            wint_t ch = 0;
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
                    struct Todo_List *item = lview->listing;
                    for (int i = lview->selection_index; i; --i)
                    {
                        item = item->next;
                    }
                    item->state = State_Not_Started;
                }
                if (ch == KEY_RIGHT)
                {
                    struct Todo_List *item = lview->listing;
                    for (int i = lview->selection_index; i; --i)
                    {
                        item = item->next;
                    }
                    switch (item->state)
                    {
                        case State_Not_Started:
                        case State_Priority:
                            item->state = State_Doing;
                            break;

                        case State_Doing:
                            item->state = State_Done;
                            break;

                        case State_Done:
                            break;
                    }
                }
                werase(stdscr);
                wrefresh(stdscr);
                draw_todo_list_view_to_window(stdscr, lview);
            } while (get_wch(&ch), ch != 'q');
            todo_list_free(lview->listing);
            free(lview);
        }
    }

    endwin();
    return 0;
}
