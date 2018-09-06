
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
	void defer_close(int *f) { close(*f); }
	void defer_free(int **p) { free(*p); }

    int fd __attribute__((cleanup (defer_close))) = open(file_path, O_RDONLY);
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

	wchar_t *wide_file_contents __attribute__((cleanup (defer_free))) = calloc(offset_end + 1, sizeof (wchar_t));
    wchar_t *start_ptr = wide_file_contents;
    swprintf(start_ptr, offset_end + 1, L"%s", file_contents);
    free(file_contents);
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
    wchar_t *space = calloc(max_x + 1, sizeof (wchar_t));
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
                week_tracker_tm = tm;
                week_tracker_tm.tm_mday -= week_tracker_tm.tm_wday;
                time_t time_at_start_of_week = mktime(&week_tracker_tm);
                if (! printed_header)
                {
                    printed_header = true;
                    goto skip_date;
                }

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
                mvwaddnwstr(window, draw_y, draw_x, config.enable_unicode ? L" \uF7C6  " : L" >  ", 4);
            }
            else
            {
                mvwaddnwstr(window, draw_y, draw_x, L"    ", 4);
            }
            draw_x += 4;

            size_t day_string_length = wcsftime(scratch_buffer,
                    ARRAY_COUNT(scratch_buffer),
                    L" %a ",
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

const char *__asan_default_options()
{
    return "";
	return
	//"quarantine_size"
		//Deprecated, please use quarantine_size_mb.
	//"quarantine_size_mb"
		//Size (in Mb) of quarantine used to detect use-after-free errors. Lower value may reduce memory usage but increase the chance of false negatives.
	//"thread_local_quarantine_size_kb"
		//Size (in Kb) of thread local quarantine used to detect use-after-free errors. Lower value may reduce memory usage but increase the chance of false negatives. It is not advised to go lower than 64Kb, otherwise frequent transfers to global quarantine might affect performance.
	//"redzone"
		//Minimal size (in bytes) of redzones around heap objects. Requirement: redzone >= 16, is a power of two.
	//"max_redzone"
		//Maximal size (in bytes) of redzones around heap objects.
	"debug=1:"
		//If set, prints some debugging information and does additional checks.
	//"report_globals"
		//Controls the way to handle globals (0 - don't detect buffer overflow on globals, 1 - detect buffer overflow, 2 - print data about registered globals).
	"check_initialization_order=1:"
		//If set, attempts to catch initialization order issues.
	"replace_str=1:"
		//If set, uses custom wrappers and replacements for libc string functions to find more errors.
	"replace_intrin=1:"
		//If set, uses custom wrappers for memset/memcpy/memmove intrinsics.
	"detect_stack_use_after_return=1:"
		//Enables stack-use-after-return checking at run-time.
	//"min_uar_stack_size_log"
		//Minimum fake stack size log.
	//"max_uar_stack_size_log"
		//Maximum fake stack size log.
	//"uar_noreserve"
		//Use mmap with 'noreserve' flag to allocate fake stack.
	//"max_malloc_fill_size"
		//ASan allocator flag. max_malloc_fill_size is the maximal amount of bytes that will be filled with malloc_fill_byte on malloc.
	//"max_free_fill_size"
		//ASan allocator flag. max_free_fill_size is the maximal amount of bytes that will be filled with free_fill_byte during free.
	//"malloc_fill_byte"
		//Value used to fill the newly allocated memory.
	//"free_fill_byte"
		//Value used to fill deallocated memory.
	//"allow_user_poisoning"
		//If set, user may manually mark memory regions as poisoned or unpoisoned.
	//"sleep_before_dying"
		//Number of seconds to sleep between printing an error report and terminating the program. Useful for debugging purposes (e.g. when one needs to attach gdb).
	//"sleep_after_init"
		//Number of seconds to sleep after AddressSanitizer is initialized. Useful for debugging purposes (e.g. when one needs to attach gdb).
	//"check_malloc_usable_size"
		//Allows the users to work around the bug in Nvidia drivers prior to 295.*.
	//"unmap_shadow_on_exit"
		//If set, explicitly unmaps the (huge) shadow at exit.
	//"protect_shadow_gap"
		//If set, mprotect the shadow gap
	"print_stats=1:"
		//Print various statistics after printing an error message or if atexit=1.
	//"print_legend"
		//Print the legend for the shadow bytes.
	//"print_scariness"
		//Print the scariness score. Experimental.
	"atexit=1:"
		//If set, prints ASan exit stats even after program terminates successfully.
	//"print_full_thread_history"
		//If set, prints thread creation stacks for the threads involved in the report and their ancestors up to the main thread.
	"poison_heap=1:"
		//Poison (or not) the heap memory on [de]allocation. Zero value is useful for benchmarking the allocator or instrumentator.
	//"poison_partial"
		//If true, poison partially addressable 8-byte aligned words (default=true). This flag affects heap and global buffers, but not stack buffers.
	"poison_array_cookie=1:"
		//Poison (or not) the array cookie after operator new[].
	//"alloc_dealloc_mismatch"
		//Report errors on malloc/delete, new/free, new/delete[], etc.
	//"new_delete_type_mismatch"
		//Report errors on mismatch between size of new and delete.
	"strict_init_order=1:"
		//If true, assume that dynamic initializers can never access globals from other modules, even if the latter are already initialized.
	//"start_deactivated"
		//If true, ASan tweaks a bunch of other flags (quarantine, redzone, heap poisoning) to reduce memory consumption as much as possible, and restores them to original values when the first instrumented module is loaded into the process. This is mainly intended to be used on Android.
	"detect_invalid_pointer_pairs=2:"
		//If >= 2, detect operations like <, <=, >, >= and - on invalid pointer pairs (e.g. when pointers belong to different objects); If == 1, detect invalid operations only when both pointers are non-null.
	//"detect_container_overflow"
		//If true, honor the container overflow annotations. See https://github.com/google/sanitizers/wiki/AddressSanitizerContainerOverflow
	//"detect_odr_violation"
		//If >=2, detect violation of One-Definition-Rule (ODR); If ==1, detect ODR-violation only if the two variables have different sizes
	//"suppressions"
		//Suppressions file name.
	//"halt_on_error"
		//Crash the program after printing the first error report (WARNING: USE AT YOUR OWN RISK!)
	//"use_odr_indicator"
		//Use special ODR indicator symbol for ODR violation detection
	//"allocator_frees_and_returns_null_on_realloc_zero"
		//realloc(p, 0) is equivalent to free(p) by default (Same as the POSIX standard). If set to false, realloc(p, 0) will return a pointer to an allocated space which can not be used.
	//"verify_asan_link_order"
		//Check position of ASan runtime in library list (needs to be disabled when other library has to be preloaded system-wide)
	//"symbolize"
		//If set, use the online symbolizer from common sanitizer runtime to turn virtual addresses to file/line locations.
	//"external_symbolizer_path"
		//Path to external symbolizer. If empty, the tool will search $PATH for the symbolizer.
	//"allow_addr2line"
		//If set, allows online symbolizer to run addr2line binary to symbolize stack traces (addr2line will only be used if llvm-symbolizer binary is unavailable.
	//"strip_path_prefix"
		//Strips this prefix from file paths in error reports.
	//"fast_unwind_on_check"
		//If available, use the fast frame-pointer-based unwinder on internal CHECK failures.
	//"fast_unwind_on_fatal"
		//If available, use the fast frame-pointer-based unwinder on fatal errors.
	//"fast_unwind_on_malloc"
		//If available, use the fast frame-pointer-based unwinder on malloc/free.
	//"handle_ioctl"
		//Intercept and handle ioctl requests.
	//"malloc_context_size"
		//Max number of stack frames kept for each allocation/deallocation.
	//"log_path"
		//Write logs to "log_path.pid". The special values are "stdout" and "stderr". The default is "stderr".
	//"log_exe_name"
		//Mention name of executable when reporting error and append executable name to logs (as in "log_path.exe_name.pid").
	//"log_to_syslog"
		//Write all sanitizer output to syslog in addition to other means of logging.
	"verbosity=2:"
		//Verbosity level (0 - silent, 1 - a bit of output, 2+ - more output).
	"detect_leaks=1:"
		//Enable memory leak detection.
	"leak_check_at_exit=1:"
		//Invoke leak checking in an atexit handler. Has no effect if detect_leaks=false, or if __lsan_do_leak_check() is called before the handler has a chance to run.
	//"allocator_may_return_null"
		//If false, the allocator will crash instead of returning 0 on out-of-memory.
	//"print_summary"
		//If false, disable printing error summaries in addition to error reports.
	//"print_module_map"
		//OS X only (0 - don't print, 1 - print only once before process exits, 2 - print after each report).
	//"check_printf"
		//Check printf arguments.
	//"handle_segv"
		//Controls custom tool's SIGSEGV handler (0 - do not registers the handler, 1 - register the handler and allow user to set own, 2 - registers the handler and block user from changing it).
	//"handle_sigbus"
		//Controls custom tool's SIGBUS handler (0 - do not registers the handler, 1 - register the handler and allow user to set own, 2 - registers the handler and block user from changing it).
	//"handle_abort"
		//Controls custom tool's SIGABRT handler (0 - do not registers the handler, 1 - register the handler and allow user to set own, 2 - registers the handler and block user from changing it).
	//"handle_sigill"
		//Controls custom tool's SIGILL handler (0 - do not registers the handler, 1 - register the handler and allow user to set own, 2 - registers the handler and block user from changing it).
	//"handle_sigfpe"
		//Controls custom tool's SIGFPE handler (0 - do not registers the handler, 1 - register the handler and allow user to set own, 2 - registers the handler and block user from changing it).
	//"allow_user_segv_handler"
		//Deprecated. True has no effect, use handle_sigbus=1. If false, handle_*=1 will be upgraded to handle_*=2.
	//"use_sigaltstack"
		//If set, uses alternate stack for signal handling.
	"detect_deadlocks=1:"
		//If set, deadlock detection is enabled.
	//"clear_shadow_mmap_threshold"
		//Large shadow regions are zero-filled using mmap(NORESERVE) instead of memset(). This is the threshold size in bytes.
	"color=always:"
		//Colorize reports: (always|never|auto).
	//"legacy_pthread_cond"
		//Enables support for dynamic libraries linked with libpthread 2.2.5.
	//"intercept_tls_get_addr"
		//Intercept __tls_get_addr.
	//"help"
		//Print the flag descriptions.
	//"mmap_limit_mb"
		//Limit the amount of mmap-ed memory (excluding shadow) in Mb; not a user-facing flag, used mosly for testing the tools
	//"hard_rss_limit_mb"
		//Hard RSS limit in Mb. If non-zero, a background thread is spawned at startup which periodically reads RSS and aborts the process if the limit is reached
	//"soft_rss_limit_mb"
		//Soft RSS limit in Mb. If non-zero, a background thread is spawned at startup which periodically reads RSS. If the limit is reached all subsequent malloc/new calls will fail or return NULL (depending on the value of allocator_may_return_null) until the RSS goes below the soft limit. This limit does not affect memory allocations other than malloc/new.
	//"heap_profile"
		//Experimental heap profiler, asan-only
	//"allocator_release_to_os_interval_ms"
		//Experimental. Only affects a 64-bit allocator. If set, tries to release unused memory to the OS, but not more often than this interval (in milliseconds). Negative values mean do not attempt to release memory to the OS.
	//"can_use_proc_maps_statm"
		//If false, do not attempt to read /proc/maps/statm. Mostly useful for testing sanitizers.
	//"coverage"
		//If set, coverage information will be dumped at program shutdown (if the coverage instrumentation was enabled at compile time).
	//"coverage_dir"
		//Target directory for coverage dumps. Defaults to the current directory.
	//"full_address_space"
		//Sanitize complete address space; by default kernel area on 32-bit platforms will not be sanitized
	//"print_suppressions"
		//Print matched suppressions at exit.
	//"disable_coredump"
		//Disable core dumping. By default, disable_coredump=1 on 64-bit to avoid dumping a 16T+ core file. Ignored on OSes that don't dump core by default and for sanitizers that don't reserve lots of virtual memory.
	//"use_madv_dontdump"
		//If set, instructs kernel to not store the (huge) shadow in core file.
	//"symbolize_inline_frames"
		//Print inlined frames in stacktraces. Defaults to true.
	//"symbolize_vs_style"
		//Print file locations in Visual Studio style (e.g:  file(10,42): ...
	//"dedup_token_length"
		//If positive, after printing a stack trace also print a short string token based on this number of frames that will simplify deduplication of the reports. Example: 'DEDUP_TOKEN: foo-bar-main'. Default is 0.
	//"stack_trace_format"
		//Format string used to render stack frames. See sanitizer_stacktrace_printer.h for the format description. Use DEFAULT to get default format.
	//"no_huge_pages_for_shadow"
		//If true, the shadow is not allowed to use huge pages.
	"strict_string_checks=1:"
		//If set check that string arguments are properly null-terminated
	"intercept_strstr=1:"
		//If set, uses custom wrappers for strstr and strcasestr functions to find more errors.
	"intercept_strspn=1:"
		//If set, uses custom wrappers for strspn and strcspn function to find more errors.
	"intercept_strtok=1:"
		//If set, uses a custom wrapper for the strtok function to find more errors.
	"intercept_strpbrk=1:"
		//If set, uses custom wrappers for strpbrk function to find more errors.
	"intercept_strlen=1:"
		//If set, uses custom wrappers for strlen and strnlen functions to find more errors.
	"intercept_strndup=1:"
		//If set, uses custom wrappers for strndup functions to find more errors.
	"intercept_strchr=1:"
		//If set, uses custom wrappers for strchr, strchrnul, and strrchr functions to find more errors.
	"intercept_memcmp=1:"
		//If set, uses custom wrappers for memcmp function to find more errors.
	"strict_memcmp=1:"
		//If true, assume that memcmp(p1, p2, n) always reads n bytes before comparing p1 and p2.
	"intercept_memmem=1:"
		//If set, uses a wrapper for memmem() to find more errors.
	"intercept_intrin=1:"
		//If set, uses custom wrappers for memset/memcpy/memmove intrinsics to find more errors.
	"intercept_stat=1:"
		//If set, uses custom wrappers for *stat functions to find more errors.
	"intercept_send=1"
		//If set, uses custom wrappers for send* functions to find more errors.
	//"decorate_proc_maps"
		//If set, decorate sanitizer mappings in /proc/self/maps with user-readable names
	//"exitcode"
		//Override the program exit status if the tool found an error
	//"abort_on_error"
		//If set, the tool calls abort() instead of _exit() after printing the error report.
	//"suppress_equal_pcs"
		//Deduplicate multiple reports for single source location in halt_on_error=false mode (asan only).
	//"print_cmdline"
		//Print command line on crash (asan only).
	//"html_cov_report"
		//Generate html coverage report.
	//"sancov_path"
		//Sancov tool location.
	//"dump_instruction_bytes"
		//If true, dump 16 bytes starting at the instruction that caused SEGV
	//"dump_registers"
		//If true, dump values of CPU registers when SEGV happens. Only available on OS X for now.
	//"include"
		//read more options from the given file
	//"include_if_exists"
		//read more options from the given file (if it exists)
;
}

int main(int argc, char *const *argv)
{
	void defer_free_char(char **p) { free(*p); }
    setlocale(LC_ALL, "");
    setlocale(LC_CTYPE, "");

    char *homedir __attribute__((cleanup(defer_free_char))) =
		({
			homedir = getenv("HOME");
			if (! homedir)
			{
				struct passwd *pw = getpwuid(getuid());
				homedir = pw->pw_dir;
			}
			char *result = calloc(strlen(homedir) + 2, 1);;
			sprintf(result, "%s/", homedir);
			result;
		});

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
            char *key = NULL, *value = NULL;
            int count;
            more_config:
            count = fscanf(config_file, "%ms %ms\n", &key, &value);
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
