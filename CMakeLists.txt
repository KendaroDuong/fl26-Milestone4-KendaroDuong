cmake_minimum_required(VERSION 3.16)
project(ai_workspace_m4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Widgets Test)
set(CMAKE_AUTOMOC ON)

# M4 starts from your own M3 project: copy your M3 src/ files here first.
# Add your new M4 source files to the target where they belong.

add_library(aiws_m2
    src/document.cpp
    src/workspace.cpp
    src/text_processor.cpp
    src/chunker.cpp
    src/corpus_index.cpp
    src/retrieval_engine.cpp
    src/context_builder.cpp
    src/processing_core.cpp
)
target_include_directories(aiws_m2 PUBLIC include)
target_link_libraries(aiws_m2 PUBLIC Threads::Threads)
target_compile_options(aiws_m2 PRIVATE -Wall -Wextra -Wpedantic)

# Everything except main(), so tests can link the application code.
add_library(aiws_app STATIC
    src/main_window.cpp
    src/workspace_widget.cpp
    src/search_widget.cpp
    src/context_widget.cpp
    src/term_inspector_widget.cpp
    src/search_result_model.cpp
    include/aiws/gui/main_window.hpp
    include/aiws/gui/resettable.hpp
    include/aiws/gui/workspace_widget.hpp
    include/aiws/gui/search_widget.hpp
    include/aiws/gui/context_widget.hpp
    include/aiws/gui/term_inspector_widget.hpp
    include/aiws/gui/search_result_model.hpp
)
target_include_directories(aiws_app PUBLIC include)
target_link_libraries(aiws_app PUBLIC aiws_m2 Qt6::Widgets)
target_compile_options(aiws_app PRIVATE -Wall -Wextra -Wpedantic)

add_executable(ai_workspace_m4 src/main.cpp)
target_link_libraries(ai_workspace_m4 PRIVATE aiws_app)
target_compile_options(ai_workspace_m4 PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()
add_executable(student_tests tests/student_tests.cpp)
target_link_libraries(student_tests PRIVATE aiws_app Qt6::Test)
add_test(NAME student_tests COMMAND student_tests)
set_tests_properties(student_tests PROPERTIES TIMEOUT 60 ENVIRONMENT QT_QPA_PLATFORM=offscreen)
