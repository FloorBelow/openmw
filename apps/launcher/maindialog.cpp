#include "maindialog.hpp"

#include <components/misc/helpviewer.hpp>
#include <components/version/version.hpp>

#include <QCloseEvent>
#include <QDebug>
#include <QDir>
#include <QMessageBox>
#include <QTextCodec>
#include <QTime>

#include <boost/program_options/options_description.hpp>
#include <boost/program_options/variables_map.hpp>
#include <components/files/conversion.hpp>
#include <components/files/qtconversion.hpp>

#include "advancedpage.hpp"
#include "datafilespage.hpp"
#include "graphicspage.hpp"
#include "playpage.hpp"
#include "settingspage.hpp"

using namespace Process;

void cfgError(const QString& title, const QString& msg)
{
    QMessageBox msgBox;
    msgBox.setWindowTitle(title);
    msgBox.setIcon(QMessageBox::Critical);
    msgBox.setStandardButtons(QMessageBox::Ok);
    msgBox.setText(msg);
    msgBox.exec();
}

Launcher::MainDialog::MainDialog(QWidget* parent)
    : QMainWindow(parent)
    , mGameSettings(mCfgMgr)
{
    setupUi(this);

    mGameInvoker = new ProcessInvoker();
    mWizardInvoker = new ProcessInvoker();

    connect(mWizardInvoker->getProcess(), &QProcess::started, this, &MainDialog::wizardStarted);

    connect(mWizardInvoker->getProcess(), qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        &MainDialog::wizardFinished);

    iconWidget->setViewMode(QListView::IconMode);
    iconWidget->setWrapping(false);
    iconWidget->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff); // Just to be sure
    iconWidget->setIconSize(QSize(48, 48));
    iconWidget->setMovement(QListView::Static);

    iconWidget->setSpacing(4);
    iconWidget->setCurrentRow(0);
    iconWidget->setFlow(QListView::LeftToRight);

    auto* helpButton = new QPushButton(tr("Help"));
    auto* playButton = new QPushButton(tr("Play"));
    buttonBox->button(QDialogButtonBox::Close)->setText(tr("Close"));
    buttonBox->addButton(helpButton, QDialogButtonBox::HelpRole);
    buttonBox->addButton(playButton, QDialogButtonBox::AcceptRole);

    connect(buttonBox, &QDialogButtonBox::rejected, this, &MainDialog::close);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &MainDialog::play);
    connect(buttonBox, &QDialogButtonBox::helpRequested, this, &MainDialog::help);

    // Remove what's this? button
    setWindowFlags(this->windowFlags() & ~Qt::WindowContextHelpButtonHint);

    createIcons();
}

Launcher::MainDialog::~MainDialog()
{
    delete mGameInvoker;
    delete mWizardInvoker;
}

void Launcher::MainDialog::createIcons()
{
    if (!QIcon::hasThemeIcon("document-new"))
        QIcon::setThemeName("tango");

    auto* playButton = new QListWidgetItem(iconWidget);
    playButton->setIcon(QIcon(":/images/openmw.png"));
    playButton->setText(tr("Play"));
    playButton->setTextAlignment(Qt::AlignCenter);
    playButton->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);

    auto* dataFilesButton = new QListWidgetItem(iconWidget);
    dataFilesButton->setIcon(QIcon(":/images/openmw-plugin.png"));
    dataFilesButton->setText(tr("Data Files"));
    dataFilesButton->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);
    dataFilesButton->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);

    auto* graphicsButton = new QListWidgetItem(iconWidget);
    graphicsButton->setIcon(QIcon(":/images/preferences-video.png"));
    graphicsButton->setText(tr("Graphics"));
    graphicsButton->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom | Qt::AlignAbsolute);
    graphicsButton->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);

    auto* settingsButton = new QListWidgetItem(iconWidget);
    settingsButton->setIcon(QIcon(":/images/preferences.png"));
    settingsButton->setText(tr("Settings"));
    settingsButton->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);
    settingsButton->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);

    auto* advancedButton = new QListWidgetItem(iconWidget);
    advancedButton->setIcon(QIcon(":/images/preferences-advanced.png"));
    advancedButton->setText(tr("Advanced"));
    advancedButton->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);
    advancedButton->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);

    connect(iconWidget, &QListWidget::currentItemChanged, this, &MainDialog::changePage);
}

void Launcher::MainDialog::createPages()
{
    // Avoid creating the widgets twice
    if (pagesWidget->count() != 0)
        return;

    mPlayPage = new PlayPage(this);
    mDataFilesPage = new DataFilesPage(mCfgMgr, mGameSettings, mLauncherSettings, this);
    mGraphicsPage = new GraphicsPage(this);
    mSettingsPage = new SettingsPage(mCfgMgr, mGameSettings, mLauncherSettings, this);
    mAdvancedPage = new AdvancedPage(mGameSettings, this);

    // Set the combobox of the play page to imitate the combobox on the datafilespage
    mPlayPage->setProfilesModel(mDataFilesPage->profilesModel());
    mPlayPage->setProfilesIndex(mDataFilesPage->profilesIndex());

    // Add the pages to the stacked widget
    pagesWidget->addWidget(mPlayPage);
    pagesWidget->addWidget(mDataFilesPage);
    pagesWidget->addWidget(mGraphicsPage);
    pagesWidget->addWidget(mSettingsPage);
    pagesWidget->addWidget(mAdvancedPage);

    // Select the first page
    iconWidget->setCurrentItem(iconWidget->item(0), QItemSelectionModel::Select);

    connect(mPlayPage, &PlayPage::playButtonClicked, this, &MainDialog::play);

    connect(mPlayPage, &PlayPage::signalProfileChanged, mDataFilesPage, &DataFilesPage::slotProfileChanged);
    connect(mDataFilesPage, &DataFilesPage::signalProfileChanged, mPlayPage, &PlayPage::setProfilesIndex);
    // Using Qt::QueuedConnection because signal is emitted in a subthread and slot is in the main thread
    connect(mDataFilesPage, &DataFilesPage::signalLoadedCellsChanged, mAdvancedPage,
        &AdvancedPage::slotLoadedCellsChanged, Qt::QueuedConnection);
}

Launcher::FirstRunDialogResult Launcher::MainDialog::showFirstRunDialog()
{
    if (!setupLauncherSettings())
        return FirstRunDialogResultFailure;

    // Dialog wizard and setup will fail if the config directory does not already exist
    const auto& userConfigDir = mCfgMgr.getUserConfigPath();
    if (!exists(userConfigDir))
    {
        if (!create_directories(userConfigDir))
        {
            cfgError(tr("Error opening OpenMW configuration file"),
                tr("<br><b>Could not create directory %0</b><br><br> \
                        Please make sure you have the right permissions \
                        and try again.<br>")
                    .arg(Files::pathToQString(canonical(userConfigDir))));
            return FirstRunDialogResultFailure;
        }
    }

    if (mLauncherSettings.value(QString("General/firstrun"), QString("true")) == QLatin1String("true"))
    {
        QMessageBox msgBox;
        msgBox.setWindowTitle(tr("First run"));
        msgBox.setIcon(QMessageBox::Question);
        msgBox.setStandardButtons(QMessageBox::NoButton);
        msgBox.setText(
            tr("<html><head/><body><p><b>Welcome to OpenMW!</b></p> \
                          <p>It is recommended to run the Installation Wizard.</p> \
                          <p>The Wizard will let you select an existing Morrowind installation, \
                          or install Morrowind for OpenMW to use.</p></body></html>"));

        QAbstractButton* wizardButton
            = msgBox.addButton(tr("Run &Installation Wizard"), QMessageBox::AcceptRole); // ActionRole doesn't work?!
        QAbstractButton* skipButton = msgBox.addButton(tr("Skip"), QMessageBox::RejectRole);

        msgBox.exec();

        if (msgBox.clickedButton() == wizardButton)
        {
            if (mWizardInvoker->startProcess(QLatin1String("openmw-wizard"), false))
                return FirstRunDialogResultWizard;
        }
        else if (msgBox.clickedButton() == skipButton)
        {
            // Don't bother setting up absent game data.
            if (setup())
                return FirstRunDialogResultContinue;
        }
        return FirstRunDialogResultFailure;
    }

    if (!setup() || !setupGameData())
    {
        return FirstRunDialogResultFailure;
    }
    return FirstRunDialogResultContinue;
}

void Launcher::MainDialog::setVersionLabel()
{
    // Add version information to bottom of the window
    Version::Version v = Version::getOpenmwVersion(mGameSettings.value("resources").toUtf8().constData());

    QString revision(QString::fromUtf8(v.mCommitHash.c_str()));
    QString tag(QString::fromUtf8(v.mTagHash.c_str()));

    versionLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    if (!v.mVersion.empty() && (revision.isEmpty() || revision == tag))
        versionLabel->setText(tr("OpenMW %1 release").arg(QString::fromUtf8(v.mVersion.c_str())));
    else
        versionLabel->setText(tr("OpenMW development (%1)").arg(revision.left(10)));

    // Add the compile date and time
    auto compileDate = QLocale(QLocale::C).toDate(QString(__DATE__).simplified(), QLatin1String("MMM d yyyy"));
    auto compileTime = QLocale(QLocale::C).toTime(QString(__TIME__).simplified(), QLatin1String("hh:mm:ss"));
    versionLabel->setToolTip(tr("Compiled on %1 %2")
                                 .arg(QLocale::system().toString(compileDate, QLocale::LongFormat),
                                     QLocale::system().toString(compileTime, QLocale::ShortFormat)));
}

bool Launcher::MainDialog::setup()
{
    if (!setupGameSettings())
        return false;

    setVersionLabel();

    mLauncherSettings.setContentList(mGameSettings);

    if (!setupGraphicsSettings())
        return false;

    // Now create the pages as they need the settings
    createPages();

    // Call this so we can exit on SDL errors before mainwindow is shown
    if (!mGraphicsPage->loadSettings())
        return false;

    loadSettings();

    return true;
}

bool Launcher::MainDialog::reloadSettings()
{
    if (!setupLauncherSettings())
        return false;

    if (!setupGameSettings())
        return false;

    mLauncherSettings.setContentList(mGameSettings);

    if (!setupGraphicsSettings())
        return false;

    if (!mSettingsPage->loadSettings())
        return false;

    if (!mDataFilesPage->loadSettings())
        return false;

    if (!mGraphicsPage->loadSettings())
        return false;

    if (!mAdvancedPage->loadSettings())
        return false;

    return true;
}

void Launcher::MainDialog::changePage(QListWidgetItem* current, QListWidgetItem* previous)
{
    if (!current)
        current = previous;

    int currentIndex = iconWidget->row(current);
    pagesWidget->setCurrentIndex(currentIndex);
    mSettingsPage->resetProgressBar();
}

bool Launcher::MainDialog::setupLauncherSettings()
{
    mLauncherSettings.clear();

    mLauncherSettings.setMultiValueEnabled(true);

    const auto userPath = Files::pathToQString(mCfgMgr.getUserConfigPath());

    QStringList paths;
    paths.append(QString(Config::LauncherSettings::sLauncherConfigFileName));
    paths.append(userPath + QString(Config::LauncherSettings::sLauncherConfigFileName));

    for (const QString& path : paths)
    {
        qDebug() << "Loading config file:" << path.toUtf8().constData();
        QFile file(path);
        if (file.exists())
        {
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            {
                cfgError(tr("Error opening OpenMW configuration file"),
                    tr("<br><b>Could not open %0 for reading</b><br><br> \
                             Please make sure you have the right permissions \
                             and try again.<br>")
                        .arg(file.fileName()));
                return false;
            }
            QTextStream stream(&file);
            stream.setCodec(QTextCodec::codecForName("UTF-8"));

            mLauncherSettings.readFile(stream);
        }
        file.close();
    }

    return true;
}

bool Launcher::MainDialog::setupGameSettings()
{
    mGameSettings.clear();

    const auto localPath = Files::pathToQString(mCfgMgr.getLocalPath());
    const auto userPath = Files::pathToQString(mCfgMgr.getUserConfigPath());
    const auto globalPath = Files::pathToQString(mCfgMgr.getGlobalPath());

    QFile file;

    auto loadFile = [&](const QString& path, bool (Config::GameSettings::*reader)(QTextStream&, bool),
                        bool ignoreContent = false) -> std::optional<bool> {
        qDebug() << "Loading config file:" << path.toUtf8().constData();
        file.setFileName(path);
        if (file.exists())
        {
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            {
                cfgError(tr("Error opening OpenMW configuration file"),
                    tr("<br><b>Could not open %0 for reading</b><br><br> \
                            Please make sure you have the right permissions \
                            and try again.<br>")
                        .arg(file.fileName()));
                return {};
            }
            QTextStream stream(&file);
            stream.setCodec(QTextCodec::codecForName("UTF-8"));

            (mGameSettings.*reader)(stream, ignoreContent);
            file.close();
            return true;
        }
        return false;
    };

    // Load the user config file first, separately
    // So we can write it properly, uncontaminated
    if (!loadFile(userPath + QLatin1String("openmw.cfg"), &Config::GameSettings::readUserFile))
        return false;

    // Now the rest - priority: user > local > global
    if (auto result = loadFile(localPath + QString("openmw.cfg"), &Config::GameSettings::readFile, true))
    {
        // Load global if local wasn't found
        if (!*result && !loadFile(globalPath + QString("openmw.cfg"), &Config::GameSettings::readFile, true))
            return false;
    }
    else
        return false;
    if (!loadFile(userPath + QString("openmw.cfg"), &Config::GameSettings::readFile))
        return false;

    return true;
}

bool Launcher::MainDialog::setupGameData()
{
    QStringList dataDirs;

    // Check if the paths actually contain data files
    for (const QString& path3 : mGameSettings.getDataDirs())
    {
        QDir dir(path3);
        QStringList filters;
        filters << "*.esp"
                << "*.esm"
                << "*.omwgame"
                << "*.omwaddon";

        if (!dir.entryList(filters).isEmpty())
            dataDirs.append(path3);
    }

    if (dataDirs.isEmpty())
    {
        QMessageBox msgBox;
        msgBox.setWindowTitle(tr("Error detecting Morrowind installation"));
        msgBox.setIcon(QMessageBox::Warning);
        msgBox.setStandardButtons(QMessageBox::NoButton);
        msgBox.setText(
            tr("<br><b>Could not find the Data Files location</b><br><br> \
                                   The directory containing the data files was not found."));

        QAbstractButton* wizardButton = msgBox.addButton(tr("Run &Installation Wizard..."), QMessageBox::ActionRole);
        QAbstractButton* skipButton = msgBox.addButton(tr("Skip"), QMessageBox::RejectRole);

        Q_UNUSED(skipButton); // Suppress compiler unused warning

        msgBox.exec();

        if (msgBox.clickedButton() == wizardButton)
        {
            if (!mWizardInvoker->startProcess(QLatin1String("openmw-wizard"), false))
                return false;
        }
    }

    return true;
}

bool Launcher::MainDialog::setupGraphicsSettings()
{
    Settings::Manager::clear(); // Ensure to clear previous settings in case we had already loaded settings.
    try
    {
        boost::program_options::variables_map variables;
        boost::program_options::options_description desc;
        mCfgMgr.addCommonOptions(desc);
        mCfgMgr.readConfiguration(variables, desc, true);
        Settings::Manager::load(mCfgMgr);
        return true;
    }
    catch (std::exception& e)
    {
        cfgError(tr("Error reading OpenMW configuration files"),
            tr("<br>The problem may be due to an incomplete installation of OpenMW.<br> \
                     Reinstalling OpenMW may resolve the problem.<br>")
                + e.what());
        return false;
    }
}

void Launcher::MainDialog::loadSettings()
{
    int width = mLauncherSettings.value(QString("General/MainWindow/width")).toInt();
    int height = mLauncherSettings.value(QString("General/MainWindow/height")).toInt();

    int posX = mLauncherSettings.value(QString("General/MainWindow/posx")).toInt();
    int posY = mLauncherSettings.value(QString("General/MainWindow/posy")).toInt();

    resize(width, height);
    move(posX, posY);
}

void Launcher::MainDialog::saveSettings()
{
    QString width = QString::number(this->width());
    QString height = QString::number(this->height());

    mLauncherSettings.setValue(QString("General/MainWindow/width"), width);
    mLauncherSettings.setValue(QString("General/MainWindow/height"), height);

    QString posX = QString::number(this->pos().x());
    QString posY = QString::number(this->pos().y());

    mLauncherSettings.setValue(QString("General/MainWindow/posx"), posX);
    mLauncherSettings.setValue(QString("General/MainWindow/posy"), posY);

    mLauncherSettings.setValue(QString("General/firstrun"), QString("false"));
}

bool Launcher::MainDialog::writeSettings()
{
    // Now write all config files
    saveSettings();
    mDataFilesPage->saveSettings();
    mGraphicsPage->saveSettings();
    mSettingsPage->saveSettings();
    mAdvancedPage->saveSettings();

    const auto& userPath = mCfgMgr.getUserConfigPath();

    if (!exists(userPath))
    {
        if (!create_directories(userPath))
        {
            cfgError(tr("Error creating OpenMW configuration directory"),
                tr("<br><b>Could not create %0</b><br><br> \
                         Please make sure you have the right permissions \
                         and try again.<br>")
                    .arg(Files::pathToQString(userPath)));
            return false;
        }
    }

    // Game settings
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QFile file(userPath / "openmw.cfg");
#else
    QFile file(Files::pathToQString(userPath / "openmw.cfg"));
#endif

    if (!file.open(QIODevice::ReadWrite | QIODevice::Text))
    {
        // File cannot be opened or created
        cfgError(tr("Error writing OpenMW configuration file"),
            tr("<br><b>Could not open or create %0 for writing</b><br><br> \
                     Please make sure you have the right permissions \
                     and try again.<br>")
                .arg(file.fileName()));
        return false;
    }

    mGameSettings.writeFileWithComments(file);
    file.close();

    // Graphics settings
    const auto settingsPath = mCfgMgr.getUserConfigPath() / "settings.cfg";
    try
    {
        Settings::Manager::saveUser(settingsPath);
    }
    catch (std::exception& e)
    {
        std::string msg = "<br><b>Error writing settings.cfg</b><br><br>" + Files::pathToUnicodeString(settingsPath)
            + "<br><br>" + e.what();
        cfgError(tr("Error writing user settings file"), tr(msg.c_str()));
        return false;
    }

    // Launcher settings
    file.setFileName(Files::pathToQString(userPath / Config::LauncherSettings::sLauncherConfigFileName));

    if (!file.open(QIODevice::ReadWrite | QIODevice::Text | QIODevice::Truncate))
    {
        // File cannot be opened or created
        cfgError(tr("Error writing Launcher configuration file"),
            tr("<br><b>Could not open or create %0 for writing</b><br><br> \
                     Please make sure you have the right permissions \
                     and try again.<br>")
                .arg(file.fileName()));
        return false;
    }

    QTextStream stream(&file);
    stream.setDevice(&file);
    stream.setCodec(QTextCodec::codecForName("UTF-8"));

    mLauncherSettings.writeFile(stream);
    file.close();

    return true;
}

void Launcher::MainDialog::closeEvent(QCloseEvent* event)
{
    writeSettings();
    event->accept();
}

void Launcher::MainDialog::wizardStarted()
{
    hide();
}

void Launcher::MainDialog::wizardFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (exitCode != 0 || exitStatus == QProcess::CrashExit)
        return qApp->quit();

    // HACK: Ensure the pages are created, else segfault
    setup();

    if (setupGameData() && reloadSettings())
        show();
}

void Launcher::MainDialog::play()
{
    if (!writeSettings())
        return qApp->quit();

    if (!mGameSettings.hasMaster())
    {
        QMessageBox msgBox;
        msgBox.setWindowTitle(tr("No game file selected"));
        msgBox.setIcon(QMessageBox::Warning);
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.setText(
            tr("<br><b>You do not have a game file selected.</b><br><br> \
                          OpenMW will not start without a game file selected.<br>"));
        msgBox.exec();
        return;
    }

    // Launch the game detached

    if (mGameInvoker->startProcess(QLatin1String("openmw"), true))
        return qApp->quit();
}

void Launcher::MainDialog::help()
{
    Misc::HelpViewer::openHelp("reference/index.html");
}
