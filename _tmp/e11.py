import sys; sys.path.insert(0, '_tmp')
import ed

ed.sub('tests/ui_check/CMakeLists.txt', '''add_ui_module(ui_check_theme modules/UiThemeCheck.cpp UiThemeCheck)''',
'''add_ui_module(ui_check_theme modules/UiThemeCheck.cpp UiThemeCheck)
add_ui_module(ui_check_daily_quote modules/UiDailyQuoteCheck.cpp UiDailyQuoteCheck)''')

ed.sub('tests/ui_check/CMakeLists.txt', '''wtfml_add_test(NAME wtfml.ui-refresh COMMAND ui_check_ui_refresh)''',
'''wtfml_add_test(NAME wtfml.ui-refresh COMMAND ui_check_ui_refresh)
wtfml_add_test(NAME wtfml.ui-daily-quote COMMAND ui_check_daily_quote)
set_tests_properties(wtfml.ui-daily-quote PROPERTIES
    LABELS "ui;theme"
    TIMEOUT 60
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;${WTFML_TEST_RUNTIME_ENVIRONMENT}"
)''')

# test-impact：新专项文件归属 ui（不进 default ⇒ 整仓 full）
ed.sub('tools/test-impact.ps1', """        'tests/ui_check/ArmorTrimChecks.cpp' = 'toolbox'""",
"""        'tests/ui_check/ArmorTrimChecks.cpp' = 'toolbox'
        # 主页「每日一言」那一行：挨着主题/字号走，归 ui 那一组。
        'tests/ui_check/modules/UiDailyQuoteCheck.cpp' = 'ui'""")
