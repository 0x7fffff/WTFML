import sys; sys.path.insert(0, '_tmp')
import ed
ed.sub('tests/ui_check/CMakeLists.txt',
'''add_ui_module(ui_check_daily_quote modules/UiDailyQuoteCheck.cpp UiDailyQuoteCheck)''',
'''add_ui_module(ui_check_daily_quote modules/UiDailyQuoteCheck.cpp UiDailyQuoteCheck)
add_ui_module(ui_check_daily_quote_shots modules/UiDailyQuoteShots.cpp UiDailyQuoteShots)  # 样张，不进 ctest''')
