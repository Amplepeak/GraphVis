#pragma once
#include <QDateTime>
#include <QDir>
#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

// A GraphVis project: a real folder on disk that is rescanned on every start.
//
// GraphVis 17 kept a project folder with datasets/, documents/literature/ and
// documents/scripts/, copied files into it, and listed whatever it found there
// whether or not the application had ever loaded it. That is what made a
// project survive between sessions, and v18 had nothing equivalent - a dataset
// existed only for as long as the process did.
//
// Two shapes are supported, because forcing one is what makes people keep their
// work outside the tool:
//
//   Managed  GraphVis creates the folder and its subfolders and copies files
//            into it. Somewhere to point a backup at.
//   Adopted  An existing folder - a thesis directory, say - is scanned where it
//            stands. Nothing is copied or moved; only a .graphvis/ metadata
//            folder is added.
//
// Either way the metadata lives in <root>/.graphvis/project.json, so a project
// is portable and a folder that GraphVis has opened is still just a folder.
class ProjectWorkspace : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool hasProject READ isOpen NOTIFY projectChanged)
    Q_PROPERTY(QString root READ root NOTIFY projectChanged)
    Q_PROPERTY(QString name READ name NOTIFY projectChanged)
    Q_PROPERTY(QString mode READ mode NOTIFY projectChanged)
    Q_PROPERTY(QString defaultProjectsRoot READ defaultProjectsRoot CONSTANT)

    // Auto-scanned contents. Each entry: name, path, suffix, sizeText,
    // modifiedText, linked (to the active group), supported.
    Q_PROPERTY(QVariantList datasets READ datasets NOTIFY contentsChanged)
    Q_PROPERTY(QVariantList literature READ literature NOTIFY contentsChanged)
    Q_PROPERTY(QVariantList scripts READ scripts NOTIFY contentsChanged)
    Q_PROPERTY(QString scanSummary READ scanSummary NOTIFY contentsChanged)

    Q_PROPERTY(QStringList groupNames READ groupNames NOTIFY groupsChanged)
    Q_PROPERTY(QString activeGroup READ activeGroup WRITE setActiveGroup NOTIFY groupsChanged)
    Q_PROPERTY(QVariantMap activeGroupContents READ activeGroupContents NOTIFY groupsChanged)
    Q_PROPERTY(QVariantList recentProjects READ recentProjects NOTIFY recentChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)

public:
    explicit ProjectWorkspace(QObject* parent=nullptr);

    bool isOpen() const{return !root_.isEmpty();}
    QString root() const{return root_;}
    QString name() const{return root_.isEmpty()?QString():QDir(root_).dirName();}
    QString mode() const{return mode_;}
    QString defaultProjectsRoot() const;

    QVariantList datasets() const{return datasets_;}
    QVariantList literature() const{return literature_;}
    QVariantList scripts() const{return scripts_;}
    QString scanSummary() const{return scanSummary_;}

    QStringList groupNames() const;
    QString activeGroup() const{return activeGroup_;}
    void setActiveGroup(const QString& value);
    QVariantMap activeGroupContents() const;
    QVariantList recentProjects() const{return recentProjects_;}
    QString status() const{return status_;}

    // Opening and creating.
    Q_INVOKABLE bool createProject(const QString& projectName,const QUrl& parentFolder=QUrl());
    Q_INVOKABLE bool openProject(const QUrl& folder);
    // Plain-path overloads. QML holds native paths from the scan, and
    // "file:///" + path breaks on both backslashes and spaces - a folder called
    // "My Documents" was enough to make it silently do nothing.
    Q_INVOKABLE bool openProjectPath(const QString& folder);
    Q_INVOKABLE bool addDatasetPath(const QString& file);
    Q_INVOKABLE bool adoptFolder(const QUrl& folder);
    Q_INVOKABLE void closeProject();
    Q_INVOKABLE void rescan();
    Q_INVOKABLE bool reopenLast();

    // Contents. In a managed project these copy the file in; in an adopted one
    // they record it where it already lives.
    Q_INVOKABLE bool addDataset(const QUrl& file);
    Q_INVOKABLE bool addLiterature(const QUrl& file);
    Q_INVOKABLE bool addScript(const QUrl& file);
    // Removal never deletes: a managed copy moves to detached/, an adopted file
    // is only unlinked from its groups. Deleting someone's data is not this
    // application's business.
    Q_INVOKABLE bool detachDataset(const QString& path);
    Q_INVOKABLE QVariantList detachedDatasets() const;
    Q_INVOKABLE bool restoreDetached(const QString& path);

    // Groups.
    Q_INVOKABLE bool createGroup(const QString& groupName);
    Q_INVOKABLE bool deleteGroup(const QString& groupName);
    Q_INVOKABLE bool linkToActiveGroup(const QStringList& paths,const QString& kind);
    Q_INVOKABLE bool unlinkFromActiveGroup(const QStringList& paths,const QString& kind);
    // Callers that rescan for themselves pass false, so one change does not
    // walk an adopted folder twice.
    bool linkToActiveGroup(const QStringList& paths,const QString& kind,bool rescanAfter);
    bool unlinkFromActiveGroup(const QStringList& paths,const QString& kind,bool rescanAfter);
    // isLinked(path, kind) was here. The datasets, literature and scripts
    // lists each carry a `linked` flag per row already, which is what the panel
    // reads, so this was a second way to ask the same question that only QML
    // could have used and never did.

    Q_INVOKABLE QString revealFolder(const QString& which) const;

signals:
    void projectChanged();
    void contentsChanged();
    void groupsChanged();
    void recentChanged();
    void statusChanged();

private:
    // A URL turned into an existing directory, or "" with the reason already
    // shown. Shared by openProject and adoptFolder.
    //
    // Private, and deliberately NOT in the signals section above, where it used
    // to sit. `signals:` expands to `public Q_SIGNALS:`, and moc writes a
    // definition for every function declared there - so this had two
    // definitions, moc's generated emitter and the real one in the .cpp, and
    // the link failed with "duplicate symbol ProjectWorkspace::validFolder".
    // Every translation unit compiled cleanly; only the link could see it.
    QString validFolder(const QUrl& folder);

    QString datasetsDir() const;
    QString literatureDir() const;
    QString scriptsDir() const;
    QString detachedDir() const;
    QString metadataDir() const;
    QString metadataFile() const;

    void setStatus(const QString& text);
    bool loadMetadata();
    bool saveMetadata() const;
    void ensureManagedLayout() const;
    void rememberRecent();
    QVariantList scanFolder(const QString& folder,const QStringList& suffixes,
                            bool recursive,const QString& kind) const;
    bool copyInto(const QUrl& file,const QString& folder,const QString& kind);

    QString root_;
    QString mode_=QStringLiteral("managed");
    QString activeGroup_;
    QString status_;
    QString scanSummary_;
    QVariantList datasets_;
    QVariantList literature_;
    QVariantList scripts_;
    QVariantList recentProjects_;
    // group name -> { "datasets": [paths], "literature": [...], "scripts": [...] }
    QVariantMap groups_;
};
