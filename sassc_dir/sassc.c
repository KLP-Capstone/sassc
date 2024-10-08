#ifdef _MSC_VER
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS 1
#endif
#ifndef _CRT_NONSTDC_NO_WARNINGS
#define _CRT_NONSTDC_NO_WARNINGS 1
#endif
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <sass.h>
#include "sassc_version.h"

#ifdef _MSC_VER
#include <crtdbg.h>
/// AvoidMessageBoxHook - Emulates hitting "retry" from an "abort, retry,
/// ignore" CRT debug report dialog. "retry" raises a regular exception.
static int AvoidMessageBoxHook(int ReportType, char* Message, int* Return) {
  // Set *Return to the retry code for the return value of _CrtDbgReport:
  // http://msdn.microsoft.com/en-us/library/8hyw4sy7(v=vs.71).aspx
  // This may also trigger just-in-time debugging via DebugBreak().
  if (Return)
    * Return = 1;
  // Don't call _CrtDbgReport.
  return true;
}
#endif

#define BUFSIZE 512
#ifdef _WIN32
#define PATH_SEP ';'
#else
#define PATH_SEP ':'
#endif

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#include <windows.h>

#define isatty(h) _isatty(h)
#define fileno(m) _fileno(m)

int get_argv_utf8(int* argc_ptr, char*** argv_ptr) {
  int argc;
  char** argv;
  wchar_t** argv_utf16 = CommandLineToArgvW(GetCommandLineW(), &argc);
  int i;
  int offset = (argc + 1) * sizeof(char*);
  int size = offset;
  for (i = 0; i < argc; i++)
    size += WideCharToMultiByte(CP_UTF8, 0, argv_utf16[i], -1, 0, 0, 0, 0);
  argv = malloc(size);
  for (i = 0; i < argc; i++) {
    argv[i] = (char*) argv + offset;
    offset += WideCharToMultiByte(CP_UTF8, 0, argv_utf16[i], -1,
      argv[i], size-offset, 0, 0);
  }
  *argc_ptr = argc;
  *argv_ptr = argv;
  return 0;
}
#else
#include <unistd.h>
#include <sysexits.h>
#endif

// error 상태, error 메시지, output 스트링, output파일을 parameter로 받는 함수
int output(int error_status, const char* error_message, const char* output_string, const char* outfile) {
    // error 발생 시 예외처리
    if (error_status) {
        if (error_message) {
            fprintf(stderr, "%s", error_message);
        } else {
            fprintf(stderr, "An error occurred; no error message available.\n");
        }
        return 1;
    } else if (output_string) { // 예외 없고, output 스트링이 존재한다면 outfile에 결과 쓰기
        if(outfile) {
            FILE* fp = fopen(outfile, "wb");
            if(!fp) {
                perror("Error opening output file");
                return 1;
            }
            if(fprintf(fp, "%s", output_string) < 0) {
                perror("Error writing to output file");
                fclose(fp);
                return 1;
            }
            fclose(fp);
        }
        else { // error 없고 output 파일 없을 때 command line에 출력
            #ifdef _WIN32
              setmode(fileno(stdout), O_BINARY);
            #endif
            printf("%s", output_string);
        }
        return 0;
    } else {
        fprintf(stderr, "Unknown internal error.\n");
        return 2;
    }
}

int compile_stdin(struct Sass_Options* options, char* outfile) {
    int ret;
    struct Sass_Data_Context* ctx;
    char buffer[BUFSIZE];
    size_t size = 1;
    char *source_string = malloc(sizeof(char) * BUFSIZE);

    if(source_string == NULL) {
        perror("Allocation failed");
        #ifdef _WIN32
            exit(ERROR_OUTOFMEMORY);
        #else
            exit(EX_OSERR); // system error (e.g., can't fork)
        #endif
    }

    source_string[0] = '\0';

    while(fgets(buffer, BUFSIZE, stdin)) {
        char *old = source_string;
        size += strlen(buffer);
        source_string = realloc(source_string, size);
        if(source_string == NULL) {
            perror("Reallocation failed");
            free(old);
            #ifdef _WIN32
                exit(ERROR_OUTOFMEMORY);
            #else
                exit(EX_OSERR); // system error (e.g., can't fork)
            #endif
        }
        strcat(source_string, buffer);
    }

    if(ferror(stdin)) {
        free(source_string);
        perror("Error reading standard input");
        #ifdef _WIN32
            exit(ERROR_READ_FAULT); //
        #else
            exit(EX_IOERR); // input/output error
        #endif
    }

    ctx = sass_make_data_context(source_string);
    struct Sass_Context* ctx_out = sass_data_context_get_context(ctx);
    sass_data_context_set_options(ctx, options);
    sass_compile_data_context(ctx);
    ret = output(
        sass_context_get_error_status(ctx_out),
        sass_context_get_error_message(ctx_out),
        sass_context_get_output_string(ctx_out),
        outfile
    );
    sass_delete_data_context(ctx);
    return ret;
}

// 컴파일 실행
// 정상 route -> option: blank, input_path: input file name, outfile: output file name
int compile_file(struct Sass_Options* options, char* input_path, char* outfile) {
    int ret;
    // sass_make_file_context: libsass_dir/include/sass/context.h 38번째 줄 정의, libsass_dir/src/sass_context.cpp 601번째 줄 구현
    // context의 내용이 input_path인 Sass_File_Context를 생성
    struct Sass_File_Context* ctx = sass_make_file_context(input_path);
    // sass_file_context_get_context: libsass_dir/include/sass/context.h 60번째 줄 정의, libsass_dir/src/sass_context.cpp 345번째 줄 구현
    // Sass_File_Contest ctx를 넣어서 Sass_Context를 추출 (왜있는 코드인진 모르겠음)
    struct Sass_Context* ctx_out = sass_file_context_get_context(ctx);
    if (outfile) sass_option_set_output_path(options, outfile); // option의 output_path를 outfile로 설정
    const char* srcmap_file = sass_option_get_source_map_file(options);
    sass_option_set_input_path(options, input_path); // option의 input_path를 parameter input_path로 설정
    sass_file_context_set_options(ctx, options); // 생성한 sass_file_context에 option 설정

    // 컴파일 진행
    sass_compile_file_context(ctx);

    // 컴파일 결과 에러검사
    ret = output(
        sass_context_get_error_status(ctx_out),
        sass_context_get_error_message(ctx_out),
        sass_context_get_output_string(ctx_out),
        outfile
    );

    if (ret == 0 && srcmap_file) {
        ret = output(
            sass_context_get_error_status(ctx_out),
            sass_context_get_error_message(ctx_out),
            sass_context_get_source_map_string(ctx_out),
            srcmap_file
        );
    }

    // 만들었던 ctx 삭제
    sass_delete_file_context(ctx);
    return ret;
}

struct
{
    char* style_string;
    int output_style;
} style_option_strings[] = {
    { "compressed", SASS_STYLE_COMPRESSED },
    { "compact", SASS_STYLE_COMPACT },
    { "expanded", SASS_STYLE_EXPANDED },
    { "nested", SASS_STYLE_NESTED }
};

#define NUM_STYLE_OPTION_STRINGS \
    sizeof(style_option_strings) / sizeof(style_option_strings[0])

void print_version() {
    printf("sassc: %s\n", SASSC_VERSION);
    printf("libsass: %s\n", libsass_version());
    printf("sass2scss: %s\n", sass2scss_version());
    printf("sass: %s\n", libsass_language_version());
}

void print_usage(char* argv0) {
    int i;
    printf("Usage: %s [options] [INPUT] [OUTPUT]\n\n", argv0);
    printf("Options:\n");
    printf("   -s, --stdin             Read input from standard input instead of an input file.\n");
    printf("   -t, --style NAME        Output style. Can be:");
    for(i = NUM_STYLE_OPTION_STRINGS - 1; i >= 0; i--) {
        printf(" %s", style_option_strings[i].style_string);
        printf(i == 0 ? ".\n" : ",");
    }
    printf("   -l, --line-numbers      Emit comments showing original line numbers.\n");
    printf("       --line-comments\n");
    printf("   -I, --load-path PATH    Set Sass import path.\n");
    printf("   -P, --plugin-path PATH  Set path to autoload plugins.\n");
    printf("   -m, --sourcemap[=TYPE]  Emit source map (auto or inline).\n");
    printf("   -M, --omit-map-comment  Omits the source map url comment.\n");
    printf("   -p, --precision         Set the precision for numbers.\n");
    printf("   -a, --sass              Treat input as indented syntax.\n");
    printf("   -v, --version           Display compiled versions.\n");
    printf("   -h, --help              Display this help message.\n");
    printf("\n");
}

void invalid_usage(char* argv0) {
    fprintf(stderr, "See '%s -h'\n", argv0);
    #ifdef _WIN32
        exit(ERROR_BAD_ARGUMENTS); // One or more arguments are not correct.
    #else
        exit(EX_USAGE); // command line usage error
    #endif

}

// 실제 처음 실행되는 함수
int main(int argc, char** argv) {
#ifdef _MSC_VER
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior( 0, _WRITE_ABORT_MSG);
    _CrtSetReportHook(AvoidMessageBoxHook);
#endif
#ifdef _WIN32
    get_argv_utf8(&argc, &argv);
#endif

    // command line에 "sassc"만 입력했을 경우
    if ((argc == 1) && isatty(fileno(stdin))) {
        print_usage(argv[0]);
        return 0;
    }

    char *outfile = 0;
    int from_stdin = 0;
    bool auto_source_map = false;
    bool generate_source_map = false;
    struct Sass_Options* options = sass_make_options();
    sass_option_set_output_style(options, SASS_STYLE_NESTED);
    sass_option_set_precision(options, 10);
    sass_option_set_include_path(options, getenv("SASS_PATH"));

    int c;
    size_t i;
    int long_index = 0;

    // "sassc -?" 의 ?마다 출력할 내용 저장
    static struct option long_options[] =
    {
        { "stdin",              no_argument,       0, 's' },
        { "load-path",          required_argument, 0, 'I' },
        { "plugin-path",        required_argument, 0, 'P' },
        { "style",              required_argument, 0, 't' },
        { "line-numbers",       no_argument,       0, 'l' },
        { "line-comments",      no_argument,       0, 'l' },
        { "sourcemap",          optional_argument, 0, 'm' },
        { "omit-map-comment",   no_argument,       0, 'M' },
        { "precision",          required_argument, 0, 'p' },
        { "version",            no_argument,       0, 'v' },
        { "sass",               no_argument,       0, 'a' },
        { "help",               no_argument,       0, 'h' },
        { NULL,                 0,                 NULL, 0}
    };

    // command line에서 하나씩 읽으면서 -option이 나타날 때까지 실행
    // file name 등이 적혀 있으면 getopt_long 함수 결과가 -1로 while문 빠져나감
    while ((c = getopt_long(argc, argv, "vhslm::Map:t:I:P:", long_options, &long_index)) != -1) {
        switch (c) {
        case 's':
            from_stdin = 1;
            break;
        case 'I':
            sass_option_push_include_path(options, optarg);
            break;
        case 'P':
            sass_option_push_plugin_path(options, optarg);
            break;
        case 't':
            for(i = 0; i < NUM_STYLE_OPTION_STRINGS; ++i) {
                if(strcmp(optarg, style_option_strings[i].style_string) == 0) {
                    sass_option_set_output_style(options, style_option_strings[i].output_style);
                    break;
                }
            }
            if(i == NUM_STYLE_OPTION_STRINGS) {
                fprintf(stderr, "Invalid argument for -t flag: '%s'. Allowed arguments are:", optarg);
                for(i = 0; i < NUM_STYLE_OPTION_STRINGS; ++i) {
                    fprintf(stderr, " %s", style_option_strings[i].style_string);
                }
                fprintf(stderr, "\n");
                invalid_usage(argv[0]);
            }
            break;
        case 'l':
            sass_option_set_source_comments(options, true);
            break;
        case 'm':
            if (optarg) { // optional argument
              if (strcmp(optarg, "auto") == 0) {
                auto_source_map = true;
              } else if (strcmp(optarg, "inline") == 0) {
                sass_option_set_source_map_embed(options, true);
              } else {
                fprintf(stderr, "Invalid argument for -m flag: '%s'. Allowed arguments are:", optarg);
                fprintf(stderr, " %s", "auto inline");
                fprintf(stderr, "\n");
                invalid_usage(argv[0]);
              }
            } else {
                auto_source_map = true;
            }
            generate_source_map = true;
            break;
        case 'M':
            sass_option_set_omit_source_map_url(options, true);
            break;
        case 'p':
            sass_option_set_precision(options, atoi(optarg)); // TODO: make this more robust
            if (sass_option_get_precision(options) < 0) sass_option_set_precision(options, 10);
            break;
        case 'a':
            sass_option_set_is_indented_syntax_src(options, true);
            break;
        case 'v':
            print_version();
            sass_delete_options(options);
            return 0;
        case 'h':
            print_usage(argv[0]);
            sass_delete_options(options);
            return 0;
        case '?':
            /* Unrecognized flag or missing an expected value */
            /* getopt should produce it's own error message for this case */
            invalid_usage(argv[0]);
        default:
            fprintf(stderr, "Unknown error while processing arguments\n");
            sass_delete_options(options);
            return 2;
        }
    }

    // main함수 parameter가 너무 많은 경우
    if(optind < argc - 2) {
        fprintf(stderr, "Error: Too many arguments.\n");
        invalid_usage(argv[0]);
    }

    int result;
    const char* dash = "-";
    // from_stdin : command line에서 sassc -s 입력 시 1로 설정. 그렇지 않으면 0으로 초기화
    if(optind < argc && strcmp(argv[optind], dash) != 0 && !from_stdin) {

        // 아직 command를 다 안 읽은 경우
        // 즉, input file까지 읽고 output 파일을 읽지 않은 경우
        if (optind + 1 < argc) {
            outfile = argv[optind + 1]; // output file name을 outfile 변수에 저장
        }

        // generate_source_map : 커맨드라인에서 sassc -m을 실행했을 경우에 true로 설정됨.
        if (generate_source_map && outfile) {
            const char* extension = ".map";
            char* source_map_file  = calloc(strlen(outfile) + strlen(extension) + 1, sizeof(char));
            strcpy(source_map_file, outfile);
            strcat(source_map_file, extension);
            sass_option_set_source_map_file(options, source_map_file);
        } else if (auto_source_map) {
            sass_option_set_source_map_embed(options, true);
        }
        // 컴파일 진행
        // printf("argv[optind]: %s\n", argv[optind]);
        result = compile_file(options, argv[optind], outfile);
    } else {
        if (optind < argc) {
            outfile = argv[optind];
        }
        result = compile_stdin(options, outfile);
    }

    // option 제거
    sass_delete_options(options);

    #ifdef _WIN32
        return result ? ERROR_INVALID_DATA : 0; // The data is invalid.
    #else
        return result ? EX_DATAERR : 0; // data format error
    #endif
}
