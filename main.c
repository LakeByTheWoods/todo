
#define _XOPEN_SOURCE_EXTENDED 1
#define NCURSES_WIDECHAR 1

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
#include <pwd.h>

/* ncurses */
#include <ncurses.h>

#define ARRAY_COUNT(a) (sizeof(a)/sizeof(*a))

static struct config
{
    _Bool enable_unicode;
    _Bool enter_graphical_mode;
    char *listfile;
} config;

enum Todo_State
{
    State_Not_Started   = 0,
    State_Priority      = 1,
    State_Doing         = 2,
    State_Done          = 3,
    State_In_Review		= 4,
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

static void todo_list_save(char *file_path, struct Todo_List *list)
{
    FILE *file = fopen(file_path, "w");

    while (list)
    {
        fwprintf(file, L"%X %X %X %X %ls\n", list->time_added, list->time_started, list->time_complete, list->state, list->text);
        list = list->next;
    }

    fclose(file);
}

static struct Todo_List *todo_list_load(char *file_path)
{
    int fd = open(file_path, O_RDONLY);
    if (fd == -1)
    {
        perror("panic");
        fprintf(stderr, "Could not open file %s\n", file_path);
    }
    assert(fd != -1);

    off_t offset_end = lseek(fd, 0, SEEK_END);
    assert(offset_end != -1);
    lseek(fd, 0, SEEK_SET);

    char *file_contents = calloc(offset_end + 1, 1);
    read(fd, file_contents, offset_end);

    struct Todo_List *result = 0;
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

        line += wcsspn(line, L" "); // reject spaces until actual text

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
        free(to_free->text);
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

int get_week_number(struct tm *tm)
{
    struct tm local = *tm;
    local.tm_mday -= local.tm_wday;
    mktime(&local);

    int prev_sunday = local.tm_yday;
    int week_count = prev_sunday / 7;
    int first_week_length = prev_sunday % 7;
    if (first_week_length)
    {
        week_count += 1;
    }
    return week_count;
}

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

    wchar_t scratch_buffer[64];
    struct tm week_tracker_tm = {0};
    struct Todo_List *listing = lview->listing;
    _Bool printed_header = false;
    while (listing)
    {
#define IS_SELECTED (lview->selection_index == list_index)
        if (list_index >= lview->scrolling)
        {
            struct tm tm;
            localtime_r(&listing->time_added, &tm);
            if (! printed_header || get_week_number(&tm) > get_week_number(&week_tracker_tm))
            {
                if (! printed_header)
                {
                    printed_header = true;
                    goto skip_date;
                }
                week_tracker_tm = tm;
                week_tracker_tm.tm_mday -= week_tracker_tm.tm_wday;
                time_t time_at_start_of_week = mktime(&week_tracker_tm);

                size_t date_string_length = wcsftime(scratch_buffer,
                        ARRAY_COUNT(scratch_buffer),
                        L" %Y %B %e",
                        &week_tracker_tm);
                //swprintf(scratch_buffer, ARRAY_COUNT(scratch_buffer), L"%i     %i   ", get_week_number(&week_tracker_tm), get_week_number(&tm));
                mvwaddnwstr(window, draw_y, draw_x, scratch_buffer, date_string_length);
                draw_x += date_string_length;

                void draw_number_suffix(int number)
                {
                    wchar_t *to_draw;
                    number %= 10;
                    switch (number)
                    {
                        case 1: to_draw = L"st"; break;
                        case 2: to_draw = L"nd"; break;
                        case 3: to_draw = L"rd"; break;
                        case 4 ... 10:
                        case 0: to_draw = L"th"; break;
                    }
                    mvwaddnwstr(window, draw_y, draw_x, to_draw, 2);
                    draw_x += 2;
                }

                draw_number_suffix(week_tracker_tm.tm_mday);

                time_t time_at_end_of_week = time_at_start_of_week + 60 * 60 * 24 * 7;
                struct tm temp_tm;
                localtime_r(&time_at_end_of_week, &temp_tm);

                date_string_length        = wcsftime(scratch_buffer,
                        ARRAY_COUNT(scratch_buffer),
                        L" .. %B %e",
                        &temp_tm);
                mvwaddnwstr(window, draw_y, draw_x, scratch_buffer, date_string_length);
                draw_x += date_string_length;

                draw_number_suffix(temp_tm.tm_mday);

                draw_y += 1;
                draw_x = 0;
                // Clear whole line
                mvwaddnwstr(window, draw_y, 0, space, max_x);
            }
            skip_date:

            if (IS_SELECTED) wattron(window, WA_STANDOUT);

            // Clear whole line
            mvwaddnwstr(window, draw_y, 0, space, max_x);

            if (IS_SELECTED)
            {
                mvwaddnwstr(window, draw_y, draw_x, config.enable_unicode ? L"\uF7C6    " : L" >  ", 4);
            }
            else
            {
                mvwaddnwstr(window, draw_y, draw_x, L"    ", 4);
            }
            draw_x += 4;

            size_t day_string_length = wcsftime(scratch_buffer,
                    ARRAY_COUNT(scratch_buffer),
                    L"%Y %m %d %a ",
                    &tm);
            mvwaddnwstr(window, draw_y, draw_x, scratch_buffer, day_string_length);
            draw_x += day_string_length;


            switch (listing->state)
            {
                case State_Not_Started:
                    {
                        wattron(window, COLOR_PAIR(1));
                        mvwaddnwstr(window, draw_y, draw_x, config.enable_unicode ? L" \uF62F  " : L" .  ", 4);
                        wattroff(window, COLOR_PAIR(1));
                        break;
                    }
                case State_Priority:
                    {
                        wattron(window, COLOR_PAIR(2));
                        mvwaddnwstr(window, draw_y, draw_x, config.enable_unicode ? L" \uFAD5  " : L" !  ", 4);
                        wattroff(window, COLOR_PAIR(2));
                        break;
                    }
                case State_Doing:
                    {
                        wattron(window, COLOR_PAIR(4));
                        mvwaddnwstr(window, draw_y, draw_x, config.enable_unicode ? L" \uF90B  " : L" O  ", 4);
                        wattroff(window, COLOR_PAIR(4));
                        break;
                    }
                case State_Done:
                    {
                        wattron(window, COLOR_PAIR(5));
                        mvwaddnwstr(window, draw_y, draw_x, config.enable_unicode ? L" \uF633  " : L" X  ", 4);
                        wattroff(window, COLOR_PAIR(5));
                        break;
                    }
                case State_In_Review:
                    {
                        wattron(window, COLOR_PAIR(3));
                        mvwaddnwstr(window, draw_y, draw_x, config.enable_unicode ? L" \uE215  " : L" Rv ", 4);
                        wattroff(window, COLOR_PAIR(3));
                        break;
                    }
            }

            draw_x += 4;

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

static struct Todo_List *todo_list_get_at(struct Todo_List *list, int index)
{
    struct Todo_List *result = list;
    for (int i = index; i; --i)
    {
        result = result->next;
    }
    return result;
}

static struct Todo_List *todo_list_view_get_selected(struct Todo_List_View *view)
{
    return todo_list_get_at(view->listing, view->selection_index);
}

static void todo_list_merge_sort(struct Todo_List **headref)
{
    struct Todo_List *head = *headref;
    if (head == NULL || head->next == NULL)
    {
        return;
    }

    void front_back_split(struct Todo_List *h, struct Todo_List **a_ptr, struct Todo_List **b_ptr)
    {
        struct Todo_List *slow = h;
        struct Todo_List *fast = h->next;
        while (fast)
        {
            fast = fast->next;
            if (fast)
            {
                slow = slow->next;
                fast = fast->next;
            }
        }
        *a_ptr = h;
        *b_ptr = slow->next;
        slow->next = NULL;
    }

    struct Todo_List *sorted_merge(struct Todo_List *a, struct Todo_List *b)
    {
        _Bool compare(struct Todo_List *first, struct Todo_List *secnd)
        {
            if (first->state == secnd->state)
            {
                // identical states, compare times
                switch(first->state)
                {
                    case State_Priority:
                    case State_Doing:
                    case State_In_Review:
                        return first->time_started > secnd->time_started;
                    case State_Not_Started:
                        return first->time_added > secnd->time_added;
                    case State_Done:
                        return first->time_complete > secnd->time_complete;
                }
                return false;
            }

            if (first->state == State_In_Review)
            {
                return true;
            }
            else if (secnd->state == State_In_Review)
            {
                return false;
            }

            if (first->state == State_Priority)
            {
                return true;
            }
            else if (secnd->state == State_Priority)
            {
                return false;
            }

            if (first->state == State_Doing)
            {
                return true;
            }
            else if (secnd->state == State_Doing)
            {
                return false;
            }

            if (first->state == State_Not_Started)
            {
                return true;
            }
            else if (secnd->state == State_Not_Started)
            {
                return false;
            }

            if (first->state == State_Done)
            {
                return true;
            }
            else
            {
                return false;
            }
            return true;
        }

        struct Todo_List *result = NULL;
        if (a == NULL)
            return b;
        else if (b == NULL)
            return a;

        if (compare(a, b))
        {
            result = a;
            result->next = sorted_merge(a->next, b);
        }
        else
        {
            result = b;
            result->next = sorted_merge(a, b->next);
        }
        return result;
    }

    struct Todo_List *a, *b;
    front_back_split(head, &a, &b);

    todo_list_merge_sort(&a);
    todo_list_merge_sort(&b);

    *headref = sorted_merge(a, b);
}

static char *get_home()
{
    char *home = getenv("HOME");
    if (! home)
    {
        struct passwd *pw = getpwuid(getuid());
        home = pw->pw_dir;
    }
    char *result = calloc(strlen(home) + 2, 1);;
    sprintf(result, "%s/", home);
    return result;
}

int main(int argc, char *const *argv)
{
    setlocale(LC_ALL, "");
    setlocale(LC_CTYPE, "");

    char *homedir = get_home();
    char *default_listfile = "todolist";
    config = (struct config){
        .listfile = strcat(strcpy(calloc(strlen(homedir) + strlen(default_listfile) + 1, 1), homedir), default_listfile),
    };

    {
        char *config_filename = ".config/todo.config";
        char *config_dir = strcat(strcpy(calloc(strlen(homedir) + strlen(config_filename) + 1, 1), homedir), config_filename);
        FILE *config_file = fopen(config_dir, "r");
        if (!config_file) printf("no config %s\n", config_dir);
        free(config_dir);
        if (config_file)
        {
            printf("Yes config\n");
            char *key = NULL, *value = NULL;
            int count;
            more_config:
            count = fscanf(config_file, "%ms %ms\n", &key, &value);
            printf("%i\n", count);
            if (count > 0)
            {
                if (strcmp(key, "unicode") == 0)
                {
                    if (strcmp(value, "true") == 0)
                    {
                        config.enable_unicode = true;
                    }
                    else if (strcmp(value, "false") == 0)
                    {
                        config.enable_unicode = false;
                    }
                    else
                    {
                        fprintf(stderr, "Unkown value for 'unicode' in config %s\n", value);
                        exit(EXIT_FAILURE);
                    }
                }
                else if (strcmp(key, "listfile") == 0)
                {
                    free(config.listfile);
                    config.listfile = strdup(value);
                }
                else
                {
                    fprintf(stderr, "Unkown key in config %s\n", key);
                    exit(EXIT_FAILURE);
                }
            }
            free(key); key = NULL;
            free(value); value = NULL;

            if (count > 0) goto more_config;
            
            fclose(config_file);
        }
    }

    config.enable_unicode = true;

    if (argc != 1)
    { // There's args to parse
        int option;
        while ((option = getopt(argc, argv, "+ugr::f::")) != -1)
        {
            if (option == 'u')
            {
                config.enable_unicode = true;
                config.enter_graphical_mode = true;
            }

            if (option == 'g')
            {
                config.enter_graphical_mode = true;
            }

            if (option == 'r')
            {
                // TODO: report
            }

            if (option == 'f')
            {
                // TODO: file name
            }
        }
    }
    else
    {
        config.enter_graphical_mode = true;
    }

    struct Todo_List *global_listing = todo_list_load(config.listfile);
    {
        // TODO: we should be pushing to front of list, not back
        for (int i = optind; i < argc; ++i)
        {
            struct timespec tp;
            clock_gettime(CLOCK_REALTIME, &tp);
            time_t t = tp.tv_sec;

            wchar_t *text = calloc(strlen(argv[i]) + 1, sizeof (wchar_t));
            mbstowcs(text, argv[i], strlen(argv[i]) + 1);

            struct Todo_List *new = calloc(1, sizeof (struct Todo_List));
            *new =
                (struct Todo_List)
                {
                    .time_added = t,
                    .time_started = 0,
                    .time_complete = 0,
                    .state = State_Not_Started,
                    .text = text,
                };
            new->next = global_listing;
            global_listing = new;
        }
    }

    if (! config.enter_graphical_mode)
    {
        goto save_and_quit;
    }

    if (! global_listing)
    {
        goto save_and_quit;
    }

    initscr();   // start up ncurses
    cbreak();    // don't wait for return when getting chars
    noecho();    // don't echo keypresses
    nonl();      // we wan't to be able to detect the return key
    clear();     // make sure screen is cleared because some implementations don't do it automatically
    curs_set(0); // set cursor invisible
    start_color();
    init_pair(1, COLOR_WHITE, COLOR_BLACK);
    init_pair(2, COLOR_RED, COLOR_BLACK);
    init_pair(3, COLOR_BLUE, COLOR_WHITE);
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
        lview->listing = global_listing;

        {
            int window_width, window_height; (void)window_width;
            wint_t ch = 0;
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
                    struct Todo_List *selected = todo_list_view_get_selected(lview);
                    selected->state = State_Not_Started;
                }
                if (ch == KEY_RIGHT)
                {
                    struct Todo_List *item = todo_list_view_get_selected(lview);
                    switch (item->state)
                    {
                        case State_Not_Started:
                        case State_Priority:
                            {
                                item->state = State_Doing;

                                struct timespec tp;
                                clock_gettime(CLOCK_REALTIME, &tp);
                                item->time_started = tp.tv_sec;
                                break;
                            }

                        case State_Doing:
                            {
                                item->state = State_In_Review;
                                break;
                            }

                        case State_In_Review:
                            {
                                item->state = State_Done;

                                struct timespec tp;
                                clock_gettime(CLOCK_REALTIME, &tp);
                                item->time_complete = tp.tv_sec;
                                break;
                            }

                        case State_Done:
                            break;
                    }
                }
                if (ch == '!')
                {
                    struct Todo_List *item = todo_list_view_get_selected(lview);
                    item->state = State_Priority;
                    struct timespec tp;
                    clock_gettime(CLOCK_REALTIME, &tp);
                    item->time_started = tp.tv_sec;
                }
                werase(stdscr);
                wrefresh(stdscr);
                draw_todo_list_view_to_window(stdscr, lview);
            } while (get_wch(&ch), ch != 'q');
            free(lview);
        }
    }

    endwin();

save_and_quit:
    if (global_listing)
    {
        todo_list_merge_sort(&global_listing);
        todo_list_save(config.listfile, global_listing);
    }
    free(config.listfile);
    todo_list_free(global_listing);
    return 0;
}
