import sys; sys.path.insert(0, '_tmp')
import ed

s = 'src/MainWindowSettings.cpp'
ed.sub(s, '''    const QSignalBlocker blockShowLog(m_ui->getShowGameLogCheck());''',
'''    const QSignalBlocker blockShowLog(m_ui->getShowGameLogCheck());
    const QSignalBlocker blockDailyQuote(m_ui->getDailyQuoteCheck());''')
ed.sub(s, '''    m_ui->getShowGameLogCheck()->setChecked(m_settings->showGameLog());''',
'''    m_ui->getShowGameLogCheck()->setChecked(m_settings->showGameLog());
    m_ui->getDailyQuoteCheck()->setChecked(m_settings->dailyQuote());''')
ed.sub(s, '''void MainWindow::onShowGameLogChanged() {
    m_launcherPreferences->showGameLogChanged();
}''',
'''void MainWindow::onShowGameLogChanged() {
    m_launcherPreferences->showGameLogChanged();
}

void MainWindow::onDailyQuoteChanged() {
    // 开关本身就是这一项的全部语义：落盘（控制器里一并写）+ 立刻生效。关掉会掐掉在飞的
    // 请求并把主页那一行收起来（不占位）；打开会按当前日期重来一遍（有缓存就不问网络）。
    m_dailyQuote->setEnabled(m_ui->getDailyQuoteCheck()->isChecked());
}''')

c = 'src/controllers/ConnectionsController.cpp'
ed.sub(c, '''    connect(m_owner->m_ui->getShowGameLogCheck(), &QCheckBox::toggled, m_owner,
            &MainWindow::onShowGameLogChanged);''',
'''    connect(m_owner->m_ui->getShowGameLogCheck(), &QCheckBox::toggled, m_owner,
            &MainWindow::onShowGameLogChanged);
    // 「每日一言」（设置 → 个性化 → 外观）：关掉立刻不显示也不请求，打开立刻取。
    connect(m_owner->m_ui->getDailyQuoteCheck(), &QCheckBox::toggled, m_owner,
            &MainWindow::onDailyQuoteChanged);''')
