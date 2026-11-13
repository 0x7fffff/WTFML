import sys; sys.path.insert(0, '_tmp')
import ed

c = 'CMakeLists.txt'
ed.sub(c, '''    src/controllers/HomeQuickController.cpp''',
'''    src/controllers/HomeQuickController.cpp
    # 主页「每日一言」：取句 / 当天缓存 / 句子池（判据在 src/util/DailyQuote.cpp）。
    src/controllers/DailyQuoteController.cpp''')
ed.sub(c, '''    src/util/ImageDecode.cpp''',
'''    src/util/DailyQuote.cpp
    src/util/ImageDecode.cpp''')
ed.sub(c, '''    src/controllers/HomeQuickController.hpp''',
'''    src/controllers/HomeQuickController.hpp
    src/controllers/DailyQuoteController.hpp''')
ed.sub(c, '''    src/util/ImageDecode.hpp''',
'''    src/util/DailyQuote.hpp
    src/util/ImageDecode.hpp''')

t = 'tests/CMakeLists.txt'
ed.sub(t, '''if(EXISTS ${REPO_ROOT}/src/util/ImageDecode.cpp)''',
'''if(EXISTS ${REPO_ROOT}/src/util/DailyQuote.cpp)
    list(APPEND PROJECT_SOURCES ${REPO_ROOT}/src/util/DailyQuote.cpp
                                ${REPO_ROOT}/src/util/DailyQuote.hpp)
endif()
if(EXISTS ${REPO_ROOT}/src/util/ImageDecode.cpp)''')
