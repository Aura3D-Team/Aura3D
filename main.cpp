// Log
#include <plog/Log.h>
#include <plog/Initializers/ConsoleInitializer.h>
#include <plog/Formatters/TxtFormatter.h>
// #include <plog/Formatters/MessageOnlyFormatter.h>
#include <plog/Appenders/ColorConsoleAppender.h>

int main(int argc, char **argv)
{
    // Initialize the console appender
    static plog::ColorConsoleAppender<plog::TxtFormatter> consoleAppender;
    plog::init(plog::debug, &consoleAppender);

    PLOG_DEBUG << "HELLO FROM DEBUG";

    return 0;
}
