#include <QAction>
#include <QTranslator>

#include <coreplugin/icore.h>
#include <coreplugin/icontext.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/actionmanager/actioncontainer.h>
#include <coreplugin/coreconstants.h>
#include <coreplugin/editormanager/editormanager.h>

#include <projectexplorer/projectexplorer.h>
#include <projectexplorer/projectnodes.h>
#include <projectexplorer/project.h>
#include <projectexplorer/taskhub.h>
#include <projectexplorer/buildmanager.h>
#include <projectexplorer/session.h>

#include <QtPlugin>

#include "QtcCppcheckPlugin.h"
#include "Constants.h"
#include "Settings.h"
#include "OptionsPage.h"
#include "TaskInfo.h"
#include "CppcheckRunner.h"

using namespace QtcCppcheck::Internal;

using namespace ProjectExplorer;
using namespace Core;

namespace
{
  //! Check if node with given type should me checked.
  bool isFileNodeCheckable (const FileNode* node)
  {
    return (!node->isGenerated () &&
            (node->fileType ()== SourceType || node->fileType () == HeaderType));
  }
}

QtcCppcheckPlugin::QtcCppcheckPlugin():
  IPlugin (), settings_ (new Settings (true)),
  runner_ (new CppcheckRunner (settings_, this))
{
  // Create your members
}

QtcCppcheckPlugin::~QtcCppcheckPlugin()
{
  // Unregister objects from the plugin manager's object pool
  // Delete members

  delete settings_;
  delete runner_;
}

bool QtcCppcheckPlugin::initialize(const QStringList &arguments, QString *errorString)
{
  // Register objects in the plugin manager's object pool
  // Load settings
  // Add actions to menus
  // Connect to other plugins' signals
  // In the initialize function, a plugin can be sure that the plugins it
  // depends on have initialized their members.

  Q_UNUSED(arguments)
  Q_UNUSED(errorString)

  initLanguage ();

  ProjectExplorer::TaskHub::addCategory (Constants::TASK_CATEGORY_ID,
                                         QLatin1String (Constants::TASK_CATEGORY_NAME));

  updateSettings ();

  OptionsPage* optionsPage = new OptionsPage (settings_);
  connect (optionsPage, SIGNAL (settingsChanged ()), SLOT (updateSettings ()));
  addAutoReleasedObject (optionsPage);

  initMenus ();
  initConnections ();
  return true;
}

void QtcCppcheckPlugin::initMenus()
{
  QAction *checkNodeAction = new QAction(tr("Scan with cppcheck"), this);
  Command *checkNodeCmd = ActionManager::registerAction(
                            checkNodeAction, Constants::ACTION_CHECK_NODE_ID,
                            Context(Core::Constants::C_EDIT_MODE));
  connect(checkNodeAction, SIGNAL(triggered()), this, SLOT(checkCurrentNode()));

#define ADD_TO_MENU(COMMAND, CONTAINER_ID) {ActionContainer *menu = ActionManager::actionContainer(CONTAINER_ID); if (menu != NULL) {menu->addAction (COMMAND);} }
  ADD_TO_MENU (checkNodeCmd, ProjectExplorer::Constants::M_FILECONTEXT);
  ADD_TO_MENU (checkNodeCmd, ProjectExplorer::Constants::M_FOLDERCONTEXT);
  ADD_TO_MENU (checkNodeCmd, ProjectExplorer::Constants::M_PROJECTCONTEXT);
  ADD_TO_MENU (checkNodeCmd, ProjectExplorer::Constants::M_SUBPROJECTCONTEXT);
#undef ADD_TO_MENU


  QAction *checkProjectAction = new QAction(tr("Check current project"), this);
  Core::Command *checkProjectCmd = ActionManager::registerAction(
                                     checkProjectAction, Constants::ACTION_CHECK_PROJECT_ID,
                                     Context(Core::Constants::C_GLOBAL));
  checkProjectCmd->setDefaultKeySequence (QKeySequence (tr ("Alt+C,Ctrl+A")));
  connect(checkProjectAction, SIGNAL(triggered()), this, SLOT(checkActiveProject()));

  QAction *checkDocumentAction = new QAction(tr("Check current document"), this);
  Command *checkDocumentCmd = ActionManager::registerAction(
                                checkDocumentAction, Constants::ACTION_CHECK_DOCUMENT_ID,
                                Context(Core::Constants::C_GLOBAL));
  checkDocumentCmd->setDefaultKeySequence (QKeySequence (tr ("Alt+C,Ctrl+D")));
  connect(checkDocumentAction, SIGNAL(triggered()), this, SLOT(checkCurrentDocument()));


  ActionContainer *menu = ActionManager::createMenu(Constants::MENU_ID);
  menu->menu()->setTitle(tr("Cppcheck"));
  menu->addAction(checkProjectCmd);
  menu->addAction(checkDocumentCmd);
  ActionManager::actionContainer(Core::Constants::M_TOOLS)->addMenu(menu);
}

void QtcCppcheckPlugin::initConnections()
{
  connect (runner_, SIGNAL (newTask (char, const QString &, const QString&, int)),
           SLOT (addTask (char, const QString &, const QString&, int)));

  connect (SessionManager::instance (),
           SIGNAL (aboutToUnloadSession(QString)),
           SLOT (handleSessionUnload ()));

  connect (SessionManager::instance (),
           SIGNAL (startupProjectChanged(ProjectExplorer::Project *)),
           SLOT (handleStartupProjectChange (ProjectExplorer::Project *)));

  // Check on build
  connect (BuildManager::instance (),
           SIGNAL (buildStateChanged(ProjectExplorer::Project *)),
           SLOT (handleBuildStateChange (ProjectExplorer::Project *)));

  // Open documents auto check.
  connect (EditorManager::documentModel (),
           SIGNAL (dataChanged(const QModelIndex &, const QModelIndex &, const QVector<int> &)),
           SLOT (handleDocumentsChange(const QModelIndex &, const QModelIndex &, const QVector<int> &)));
  connect (EditorManager::documentModel (),
           SIGNAL (rowsAboutToBeRemoved(const QModelIndex &, int, int)),
           SLOT (handleDocumentsClose(const QModelIndex &, int, int)));
}

void QtcCppcheckPlugin::initLanguage()
{
  const QString& language = Core::ICore::userInterfaceLanguage();
  if (!language.isEmpty())
  {
    QTranslator* translator = new QTranslator (this);
    const QString& creatorTrPath = ICore::resourcePath () + QLatin1String ("/translations");
    const QString& trFile = QLatin1String ("QtcCppcheck_") + language;
    if (translator->load (trFile, creatorTrPath))
    {
      qApp->installTranslator (translator);
    }
  }
}

void QtcCppcheckPlugin::extensionsInitialized()
{
  // Retrieve objects from the plugin manager's object pool
  // In the extensionsInitialized function, a plugin can be sure that all
  // plugins that depend on it are completely initialized.
}

ExtensionSystem::IPlugin::ShutdownFlag QtcCppcheckPlugin::aboutToShutdown()
{
  // Save settings
  // Disconnect from signals that are not needed during shutdown
  // Hide UI (if you add UI that is not in the main window directly)
  return SynchronousShutdown;
}

void QtcCppcheckPlugin::checkFiles(const QStringList &fileNames)
{
  Q_ASSERT (runner_ != NULL);
  Q_ASSERT (!fileNames.isEmpty ());
  clearTasksForFiles (fileNames);
  runner_->checkFiles (fileNames);
}

void QtcCppcheckPlugin::checkCurrentDocument()
{
  IDocument* document = EditorManager::currentDocument ();
  if (document == NULL)
  {
    return;
  }
  // Check event if it not belongs to active project.
  checkFiles (QStringList () << document->filePath ());
}

void QtcCppcheckPlugin::checkActiveProject()
{
  if (!projectFileList_.isEmpty ())
  {
    checkFiles (projectFileList_);
  }
}

void QtcCppcheckPlugin::checkCurrentNode()
{
  Node* node = ProjectExplorerPlugin::instance ()->currentNode ();
  if (node == NULL)
  {
    return;
  }

  QStringList files = checkableFiles (node);
  if (!files.isEmpty ())
  {
    checkFiles (files);
  }
}

QStringList QtcCppcheckPlugin::checkableFiles(const Node *node) const
{
  QStringList files;
  switch (node->nodeType ())
  {
    case FileNodeType:
    {
      const FileNode* file = (const FileNode*) node;
      if (isFileNodeCheckable (file))
      {
        files << file->path ();
      }
    }
      break;

    case ProjectNodeType:
    case FolderNodeType:
    case VirtualFolderNodeType:
    {
      const FolderNode* folder = (const FolderNode*) node;
      foreach (const FolderNode* subfolder, folder->subFolderNodes ())
      {
        files += checkableFiles (subfolder);
      }
      foreach (const FileNode* file, folder->fileNodes ())
      {
        files += checkableFiles (file);
      }
    }
      break;

    default:
      break;
  }
  return files;
}

void QtcCppcheckPlugin::handleStartupProjectChange(Project *project)
{
  if (!activeProject_.isNull ())
  {
    disconnect (activeProject_.data (), SIGNAL (fileListChanged ()),
                this, SLOT (handleProjectFileListChanged ()));
  }
  activeProject_ = project;
  handleProjectFileListChanged ();
  Q_ASSERT (runner_ != NULL);
  runner_->stopCheckhig ();
  if (project == NULL)
  {
    return;
  }
  connect (project, SIGNAL (fileListChanged ()), SLOT (handleProjectFileListChanged ()));

  Q_ASSERT (settings_ != NULL);
  if (settings_->checkOnProjectChange ())
  {
    checkActiveProject ();
  }
}

void QtcCppcheckPlugin::handleProjectFileListChanged()
{
  if (activeProject_.isNull ())
  {
    projectFileList_.clear ();
    clearTasksForFiles ();
    return;
  }

  QStringList oldFiles = projectFileList_;
  Q_ASSERT (activeProject_->rootProjectNode () != NULL);
  projectFileList_ = checkableFiles (activeProject_->rootProjectNode ());
  QStringList addedFiles;
  foreach (const QString& file, projectFileList_)
  {
    if (oldFiles.contains (file))
    {
      oldFiles.removeAll (file);
      continue;
    }
    addedFiles << file;
  }
  clearTasksForFiles (oldFiles); // Removed files.

  if (settings_->checkOnFileAdd ())
  {
    checkFiles (addedFiles);
  }
}

void QtcCppcheckPlugin::handleSessionUnload()
{
  clearTasksForFiles ();
  Q_ASSERT (runner_ != NULL);
  runner_->stopCheckhig ();
}

void QtcCppcheckPlugin::handleBuildStateChange(Project *project)
{
  Q_ASSERT (settings_ != NULL);
  if (!settings_->checkOnBuild () ||
      project == NULL || project != activeProject_.data ())
  {
    return;
  }
  if (!BuildManager::isBuilding (activeProject_.data ())) // Finished building.
  {
    checkActiveProject ();
  }
}

void QtcCppcheckPlugin::handleDocumentsChange(const QModelIndex &topLeft,
                                              const QModelIndex &bottomRight,
                                              const QVector<int> &roles)
{
  Q_UNUSED (roles);
  checkActiveProjectDocuments (topLeft.row (), bottomRight.row (), false);
}

void QtcCppcheckPlugin::handleDocumentsClose(const QModelIndex &parent,
                                             int start, int end)
{
  Q_UNUSED (parent);
  checkActiveProjectDocuments (start, end, true); // Documents were modified before remove.
}

void QtcCppcheckPlugin::checkActiveProjectDocuments(int beginRow, int endRow,
                                                    bool modifiedFlag)
{
  DocumentModel* model = EditorManager::documentModel ();
  Q_ASSERT (model != NULL);
  QStringList filesToCheck;
  for (int row = beginRow; row <= endRow; ++row)
  {
    DocumentModel::Entry* entry = model->documentAtRow (row);
    if (entry == NULL)
    {
      continue;
    }
    IDocument* document = entry->document;
    if (document == NULL)
    {
      continue;
    }
    if (projectFileList_.contains (document->filePath ()) &&
        document->isModified () == modifiedFlag)
    {
      filesToCheck << document->filePath ();
    }
  }

  if (!filesToCheck.isEmpty ())
  {
    checkFiles (filesToCheck);
  }
}

void QtcCppcheckPlugin::addTask(char type, const QString &description,
                                const QString &fileName, int line)
{
  QFileInfo info(fileName);
  if (!info.exists ()) // Not points to file.
  {
    return;
  }
  Utils::FileName file (info);
  QString fullDescription = QLatin1String (Constants::TASK_CATEGORY_NAME) +
                            QLatin1String (": ") + description;
  TaskInfo taskInfo (line, fullDescription);
  // Search for duplicates (see TaskInfo class description).
  if (fileTasks_.values (fileName).contains (taskInfo))
  {
    return;
  }

  Task::TaskType taskType = (type == 'e') ? Task::Error : Task::Warning;
  Task task (taskType, fullDescription, file, line, Constants::TASK_CATEGORY_ID);
  TaskHub::addTask (task);
  TaskHub::requestPopup ();

  taskInfo = task;
  fileTasks_.insertMulti (fileName, taskInfo);
}

void QtcCppcheckPlugin::clearTasksForFiles(const QStringList &fileList)
{
  if (fileList.isEmpty ())
  {
    TaskHub::clearTasks (Constants::TASK_CATEGORY_ID);
    fileTasks_.clear ();
  }
  else
  {
    foreach (const QString& file, fileList)
    {
      if (!fileTasks_.contains (file))
      {
        continue;
      }
      Task task;
      foreach (const TaskInfo& info, fileTasks_.values (file))
      {
        info.init (task);
        TaskHub::removeTask (task);
      }
      fileTasks_.remove (file);
    }
  }
}

void QtcCppcheckPlugin::updateSettings()
{
  Q_ASSERT (runner_ != NULL);
  runner_->updateSettings ();
}

Q_EXPORT_PLUGIN2(QtcCppcheck, QtcCppcheckPlugin)

