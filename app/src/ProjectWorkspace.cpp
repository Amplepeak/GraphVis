#include "ProjectWorkspace.h"
#include "AppController.h"

#include <QSet>
#include <QDesktopServices>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>

namespace {

QString cleanFolder(const QUrl& url){
    if(url.isEmpty()) return {};
    return url.isLocalFile()?url.toLocalFile():url.toString();
}

QSettings projectSettings(){
    return QSettings(QStringLiteral("GraphVis"),QStringLiteral("GraphVis 18.4"));
}

// Directories that are never anyone's research data. Walking into them turns
// adopting a folder from a one-second scan into a minute of churn - .tooling
// alone holds 22,000 files of vcpkg checkout.
bool isNoiseDirectory(const QString& name){
    static const QStringList skip{
        QStringLiteral(".graphvis"),QStringLiteral(".git"),QStringLiteral(".svn"),
        QStringLiteral(".hg"),QStringLiteral("node_modules"),QStringLiteral("__pycache__"),
        QStringLiteral(".venv"),QStringLiteral("venv"),QStringLiteral("build"),
        QStringLiteral("dist"),QStringLiteral(".idea"),QStringLiteral(".vscode"),
        QStringLiteral(".tooling"),QStringLiteral(".cache"),QStringLiteral("vcpkg_installed"),
        QStringLiteral("detached"),QStringLiteral("backups"),QStringLiteral("$RECYCLE.BIN"),
    };
    return skip.contains(name,Qt::CaseInsensitive);
}

QString humanSize(qint64 bytes){
    return QLocale().formattedDataSize(bytes,1,QLocale::DataSizeTraditionalFormat);
}

QString humanWhen(const QDateTime& when){
    const qint64 days=when.daysTo(QDateTime::currentDateTime());
    if(days<=0) return when.toString(QStringLiteral("HH:mm"));
    if(days==1) return QStringLiteral("yesterday");
    if(days<7) return QStringLiteral("%1 days ago").arg(days);
    return when.toString(QStringLiteral("d MMM yyyy"));
}

// A scan has to stop somewhere. An adopted folder can be a whole drive.
constexpr int kMaxScannedFiles=4000;
constexpr int kMaxAdoptedDepth=6;

} // namespace

ProjectWorkspace::ProjectWorkspace(QObject* parent):QObject(parent){
    recentProjects_=projectSettings().value(QStringLiteral("project/recent")).toList();
}

QString ProjectWorkspace::defaultProjectsRoot() const{
    const QString docs=QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return (docs.isEmpty()?QDir::homePath():docs)+QStringLiteral("/GraphVis Projects");
}

QString ProjectWorkspace::datasetsDir() const{
    if(root_.isEmpty()) return {};
    return mode_==QStringLiteral("managed")?root_+QStringLiteral("/datasets"):root_;
}
QString ProjectWorkspace::literatureDir() const{
    if(root_.isEmpty()) return {};
    return mode_==QStringLiteral("managed")?root_+QStringLiteral("/documents/literature"):root_;
}
QString ProjectWorkspace::scriptsDir() const{
    if(root_.isEmpty()) return {};
    return mode_==QStringLiteral("managed")?root_+QStringLiteral("/documents/scripts"):root_;
}
QString ProjectWorkspace::detachedDir() const{
    return root_.isEmpty()?QString():root_+QStringLiteral("/detached");
}
QString ProjectWorkspace::metadataDir() const{
    return root_.isEmpty()?QString():root_+QStringLiteral("/.graphvis");
}
QString ProjectWorkspace::metadataFile() const{
    return root_.isEmpty()?QString():metadataDir()+QStringLiteral("/project.json");
}

void ProjectWorkspace::setStatus(const QString& text){
    if(status_==text) return;
    status_=text;
    emit statusChanged();
}

void ProjectWorkspace::ensureManagedLayout() const{
    if(root_.isEmpty()||mode_!=QStringLiteral("managed")) return;
    for(const QString& sub:{QStringLiteral("datasets"),
                            QStringLiteral("documents/literature"),
                            QStringLiteral("documents/scripts"),
                            QStringLiteral("detached"),
                            QStringLiteral("exports")})
        QDir().mkpath(root_+QLatin1Char('/')+sub);
}

bool ProjectWorkspace::saveMetadata() const{
    if(root_.isEmpty()) return false;
    QDir().mkpath(metadataDir());
    QJsonObject groups;
    for(auto it=groups_.constBegin();it!=groups_.constEnd();++it){
        const QVariantMap members=it.value().toMap();
        QJsonObject entry;
        for(const QString& kind:{QStringLiteral("datasets"),QStringLiteral("literature"),QStringLiteral("scripts")})
            entry.insert(kind,QJsonArray::fromStringList(members.value(kind).toStringList()));
        groups.insert(it.key(),entry);
    }
    const QJsonObject doc{
        {QStringLiteral("version"),1},
        {QStringLiteral("mode"),mode_},
        {QStringLiteral("activeGroup"),activeGroup_},
        {QStringLiteral("groups"),groups},
    };
    QFile f(metadataFile());
    if(!f.open(QIODevice::WriteOnly|QIODevice::Truncate)) return false;
    f.write(QJsonDocument(doc).toJson(QJsonDocument::Indented));
    return true;
}

bool ProjectWorkspace::loadMetadata(){
    groups_.clear();
    activeGroup_.clear();
    QFile f(metadataFile());
    if(!f.open(QIODevice::ReadOnly)) return false;
    const QJsonObject doc=QJsonDocument::fromJson(f.readAll()).object();
    mode_=doc.value(QStringLiteral("mode")).toString(mode_);
    activeGroup_=doc.value(QStringLiteral("activeGroup")).toString();
    const QJsonObject groups=doc.value(QStringLiteral("groups")).toObject();
    for(auto it=groups.constBegin();it!=groups.constEnd();++it){
        const QJsonObject entry=it.value().toObject();
        QVariantMap members;
        for(const QString& kind:{QStringLiteral("datasets"),QStringLiteral("literature"),QStringLiteral("scripts")}){
            QStringList paths;
            for(const QJsonValue v:entry.value(kind).toArray()) paths<<v.toString();
            members.insert(kind,paths);
        }
        groups_.insert(it.key(),members);
    }
    if(!groups_.contains(activeGroup_)) activeGroup_.clear();
    return true;
}

void ProjectWorkspace::rememberRecent(){
    if(root_.isEmpty()) return;
    QVariantList kept;
    kept.append(QVariantMap{{QStringLiteral("path"),root_},
                            {QStringLiteral("name"),name()},
                            {QStringLiteral("mode"),mode_}});
    for(const QVariant& v:std::as_const(recentProjects_)){
        const QVariantMap entry=v.toMap();
        if(entry.value(QStringLiteral("path")).toString()==root_) continue;
        if(!QFileInfo::exists(entry.value(QStringLiteral("path")).toString())) continue;
        kept.append(entry);
        if(kept.size()>=8) break;
    }
    recentProjects_=kept;
    auto s=projectSettings();
    s.setValue(QStringLiteral("project/recent"),recentProjects_);
    s.setValue(QStringLiteral("project/last"),root_);
    emit recentChanged();
}

bool ProjectWorkspace::createProject(const QString& projectName,const QUrl& parentFolder){
    const QString trimmed=projectName.trimmed();
    if(trimmed.isEmpty()){setStatus(QStringLiteral("A project needs a name"));return false;}
    // Reject the characters Windows will not accept rather than letting mkpath
    // fail with nothing to show for it.
    if(trimmed.contains(QRegularExpression(QStringLiteral(R"([<>:"/\\|?*])")))){
        setStatus(QStringLiteral("A project name cannot contain < > : \" / \\ | ? *"));
        return false;
    }
    const QString parent=parentFolder.isEmpty()?defaultProjectsRoot():cleanFolder(parentFolder);
    const QString target=parent+QLatin1Char('/')+trimmed;
    if(QFileInfo::exists(target)){
        setStatus(QStringLiteral("A folder called \"%1\" is already there").arg(trimmed));
        return false;
    }
    if(!QDir().mkpath(target)){
        setStatus(QStringLiteral("Could not create %1").arg(target));
        return false;
    }
    root_=QDir(target).absolutePath();
    mode_=QStringLiteral("managed");
    groups_.clear();
    activeGroup_.clear();
    ensureManagedLayout();
    saveMetadata();
    rememberRecent();
    emit projectChanged();
    emit groupsChanged();
    rescan();
    setStatus(QStringLiteral("Created project \"%1\"").arg(trimmed));
    return true;
}

// Both ways into a project - opening one and adopting a folder - start by
// turning a URL into a directory that exists, and say the same thing when it
// is not one.
QString ProjectWorkspace::validFolder(const QUrl& folder){
    const QString path=cleanFolder(folder);
    if(path.isEmpty()||!QFileInfo(path).isDir()){
        setStatus(QStringLiteral("That is not a folder"));
        return QString();
    }
    return path;
}

bool ProjectWorkspace::openProject(const QUrl& folder){
    const QString path=validFolder(folder);
    if(path.isEmpty()) return false;
    root_=QDir(path).absolutePath();
    // A folder GraphVis has opened before carries its own mode; anything else
    // is adopted, because copying a stranger's files around uninvited is not on.
    mode_=QStringLiteral("adopted");
    if(!loadMetadata()){
        const bool looksManaged=QFileInfo::exists(root_+QStringLiteral("/datasets"))
                                &&QFileInfo::exists(root_+QStringLiteral("/documents"));
        mode_=looksManaged?QStringLiteral("managed"):QStringLiteral("adopted");
        saveMetadata();
    }
    ensureManagedLayout();
    rememberRecent();
    emit projectChanged();
    emit groupsChanged();
    rescan();
    setStatus(QStringLiteral("Opened %1").arg(name()));
    return true;
}

bool ProjectWorkspace::openProjectPath(const QString& folder){
    return openProject(QUrl::fromLocalFile(folder));
}

bool ProjectWorkspace::addDatasetPath(const QString& file){
    return addDataset(QUrl::fromLocalFile(file));
}

bool ProjectWorkspace::adoptFolder(const QUrl& folder){
    const QString path=validFolder(folder);
    if(path.isEmpty()) return false;
    root_=QDir(path).absolutePath();
    mode_=QStringLiteral("adopted");
    loadMetadata();
    mode_=QStringLiteral("adopted");   // adopting is explicit and wins
    saveMetadata();
    rememberRecent();
    emit projectChanged();
    emit groupsChanged();
    rescan();
    setStatus(QStringLiteral("Adopted %1 - files stay where they are").arg(name()));
    return true;
}

void ProjectWorkspace::closeProject(){
    root_.clear();
    groups_.clear();
    activeGroup_.clear();
    datasets_.clear(); literature_.clear(); scripts_.clear();
    scanSummary_.clear();
    projectSettings().remove(QStringLiteral("project/last"));
    emit projectChanged();
    emit contentsChanged();
    emit groupsChanged();
    setStatus(QStringLiteral("No project open"));
}

bool ProjectWorkspace::reopenLast(){
    const QString last=projectSettings().value(QStringLiteral("project/last")).toString();
    if(last.isEmpty()||!QFileInfo(last).isDir()) return false;
    return openProject(QUrl::fromLocalFile(last));
}

QVariantList ProjectWorkspace::scanFolder(const QString& folder,const QStringList& suffixes,
                                          bool recursive,const QString& kind) const{
    QVariantList out;
    if(folder.isEmpty()||!QFileInfo(folder).isDir()) return out;
    const QDir base(folder);
    QDirIterator it(folder,QDir::Files|QDir::NoDotAndDotDot,
                    recursive?QDirIterator::Subdirectories:QDirIterator::NoIteratorFlags);
    // Hoisted out of the loop. The suffix filter was a linear scan of 149
    // strings per file, and isLinked() built a fresh QStringList of every
    // linked path in the active group per file and then scanned that too - at
    // the 4000-file cap, roughly 600,000 string comparisons and 4000 list
    // constructions for one rescan.
    QSet<QString> suffixSet;
    for(const QString& e:suffixes) suffixSet.insert(e.toLower());
    QSet<QString> linkedSet;
    if(!activeGroup_.isEmpty())
        for(const QString& linked:groups_.value(activeGroup_).toMap().value(kind).toStringList())
            linkedSet.insert(linked.toLower());

    // Sorted on the name once, rather than reconstructing two QVariantMaps and
    // two QStrings inside every comparison of an O(n log n) sort.
    QVector<QPair<QString,QVariant>> rows;

    while(it.hasNext()){
        const QString path=it.next();
        const QFileInfo info(path);
        const QString relative=base.relativeFilePath(path);
        // Skip anything under a noise directory, and anything too deep.
        const QStringList parts=relative.split(QLatin1Char('/'));
        bool skip=false;
        for(int i=0;i<parts.size()-1;++i)
            if(isNoiseDirectory(parts.at(i))){skip=true;break;}
        if(skip) continue;
        if(parts.size()-1>kMaxAdoptedDepth) continue;
        const QString suffix=info.suffix().toLower();
        if(!suffixSet.isEmpty()&&!suffixSet.contains(suffix)) continue;
        const QString absolute=info.absoluteFilePath();
        rows.append({relative.toLower(),QVariantMap{
            {QStringLiteral("name"),relative},
            {QStringLiteral("fileName"),info.fileName()},
            {QStringLiteral("path"),absolute},
            {QStringLiteral("suffix"),suffix},
            {QStringLiteral("sizeText"),humanSize(info.size())},
            {QStringLiteral("modifiedText"),humanWhen(info.lastModified())},
            {QStringLiteral("linked"),linkedSet.contains(absolute.toLower())},
        }});
        if(rows.size()>=kMaxScannedFiles) break;
    }
    std::sort(rows.begin(),rows.end(),
              [](const QPair<QString,QVariant>& a,const QPair<QString,QVariant>& b){
                  return a.first<b.first;
              });
    out.reserve(rows.size());
    for(const auto& row:rows) out.append(row.second);
    return out;
}

void ProjectWorkspace::rescan(){
    datasets_.clear(); literature_.clear(); scripts_.clear();
    if(root_.isEmpty()){
        scanSummary_=QStringLiteral("No project open");
        emit contentsChanged();
        return;
    }
    const bool adopted=(mode_==QStringLiteral("adopted"));

    QStringList datasetSuffixes;
    for(const QString& e:AppController::importableExtensions()) datasetSuffixes<<e;
    datasets_=scanFolder(datasetsDir(),datasetSuffixes,true,QStringLiteral("datasets"));
    literature_=scanFolder(literatureDir(),{QStringLiteral("pdf")},true,QStringLiteral("literature"));
    scripts_=scanFolder(scriptsDir(),{QStringLiteral("m"),QStringLiteral("py"),QStringLiteral("r"),
                                      QStringLiteral("jl"),QStringLiteral("ipynb")},
                        true,QStringLiteral("scripts"));

    // In an adopted folder a .pdf is literature and a .txt is a script, but both
    // are also dataset-readable extensions, so the same file would appear twice.
    // Datasets win only where the extension is not claimed by a more specific
    // kind.
    if(adopted){
        QVariantList filtered;
        for(const QVariant& v:std::as_const(datasets_)){
            const QString suffix=v.toMap().value(QStringLiteral("suffix")).toString();
            if(suffix==QLatin1String("pdf")) continue;
            filtered.append(v);
        }
        datasets_=filtered;
    }

    scanSummary_=QStringLiteral("%1 dataset%2, %3 paper%4, %5 script%6%7")
        .arg(datasets_.size()).arg(datasets_.size()==1?QString():QStringLiteral("s"))
        .arg(literature_.size()).arg(literature_.size()==1?QString():QStringLiteral("s"))
        .arg(scripts_.size()).arg(scripts_.size()==1?QString():QStringLiteral("s"))
        .arg(adopted?QStringLiteral(" - scanned in place"):QString());
    emit contentsChanged();
}

// ------------------------------------------------------------------ contents
bool ProjectWorkspace::copyInto(const QUrl& file,const QString& folder,const QString& kind){
    const QString source=cleanFolder(file);
    if(source.isEmpty()||!QFileInfo::exists(source)){
        setStatus(QStringLiteral("That file no longer exists"));
        return false;
    }
    if(root_.isEmpty()){
        setStatus(QStringLiteral("Open or create a project first"));
        return false;
    }

    // Adopted projects never copy. The whole point is that the user's files stay
    // where the user put them.
    if(mode_==QStringLiteral("adopted")){
        const QString absolute=QFileInfo(source).absoluteFilePath();
        if(!absolute.startsWith(QDir(root_).absolutePath(),Qt::CaseInsensitive)){
            setStatus(QStringLiteral("%1 is outside this project's folder. "
                                     "An adopted project only tracks files inside it.")
                          .arg(QFileInfo(source).fileName()));
            return false;
        }
        linkToActiveGroup({absolute},kind,false);
        rescan();
        setStatus(QStringLiteral("Tracking %1").arg(QFileInfo(source).fileName()));
        return true;
    }

    QDir().mkpath(folder);
    const QFileInfo info(source);
    QString target=folder+QLatin1Char('/')+info.fileName();
    // Never silently overwrite: a second file of the same name becomes _2.
    if(QFileInfo::exists(target)){
        int n=2;
        forever{
            const QString candidate=QStringLiteral("%1/%2_%3%4")
                .arg(folder,info.completeBaseName(),QString::number(n),
                     info.suffix().isEmpty()?QString():QLatin1Char('.')+info.suffix());
            if(!QFileInfo::exists(candidate)){target=candidate;break;}
            ++n;
        }
    }
    if(!QFile::copy(source,target)){
        setStatus(QStringLiteral("Could not copy %1 into the project").arg(info.fileName()));
        return false;
    }
    linkToActiveGroup({QFileInfo(target).absoluteFilePath()},kind,false);
    rescan();
    setStatus(QStringLiteral("Added %1").arg(QFileInfo(target).fileName()));
    return true;
}

bool ProjectWorkspace::addDataset(const QUrl& file){
    return copyInto(file,datasetsDir(),QStringLiteral("datasets"));
}
bool ProjectWorkspace::addLiterature(const QUrl& file){
    return copyInto(file,literatureDir(),QStringLiteral("literature"));
}
bool ProjectWorkspace::addScript(const QUrl& file){
    return copyInto(file,scriptsDir(),QStringLiteral("scripts"));
}

bool ProjectWorkspace::detachDataset(const QString& path){
    if(root_.isEmpty()||path.isEmpty()) return false;
    unlinkFromActiveGroup({path},QStringLiteral("datasets"),false);

    // An adopted project's files are not ours to move.
    if(mode_==QStringLiteral("adopted")){
        rescan();
        setStatus(QStringLiteral("Unlinked %1 - the file itself was left alone")
                      .arg(QFileInfo(path).fileName()));
        return true;
    }
    QDir().mkpath(detachedDir());
    const QFileInfo info(path);
    QString target=detachedDir()+QLatin1Char('/')+info.fileName();
    int n=2;
    while(QFileInfo::exists(target)){
        target=QStringLiteral("%1/%2_%3.%4").arg(detachedDir(),info.completeBaseName(),
                                                 QString::number(n++),info.suffix());
    }
    if(!QFile::rename(path,target)){
        setStatus(QStringLiteral("Could not move %1 out of the scanned folder").arg(info.fileName()));
        return false;
    }
    rescan();
    setStatus(QStringLiteral("Moved %1 to detached/ - it can be restored").arg(info.fileName()));
    return true;
}

QVariantList ProjectWorkspace::detachedDatasets() const{
    return scanFolder(detachedDir(),{},false,QStringLiteral("datasets"));
}

bool ProjectWorkspace::restoreDetached(const QString& path){
    if(root_.isEmpty()||path.isEmpty()) return false;
    const QFileInfo info(path);
    QDir().mkpath(datasetsDir());
    QString target=datasetsDir()+QLatin1Char('/')+info.fileName();
    int n=2;
    while(QFileInfo::exists(target)){
        target=QStringLiteral("%1/%2_%3.%4").arg(datasetsDir(),info.completeBaseName(),
                                                 QString::number(n++),info.suffix());
    }
    if(!QFile::rename(path,target)){
        setStatus(QStringLiteral("Could not restore %1").arg(info.fileName()));
        return false;
    }
    rescan();
    setStatus(QStringLiteral("Restored %1").arg(QFileInfo(target).fileName()));
    return true;
}

// -------------------------------------------------------------------- groups
QStringList ProjectWorkspace::groupNames() const{
    QStringList names=groups_.keys();
    std::sort(names.begin(),names.end(),[](const QString& a,const QString& b){
        return a.compare(b,Qt::CaseInsensitive)<0;
    });
    return names;
}

QVariantMap ProjectWorkspace::activeGroupContents() const{
    return groups_.value(activeGroup_).toMap();
}

void ProjectWorkspace::setActiveGroup(const QString& value){
    if(activeGroup_==value) return;
    if(!value.isEmpty()&&!groups_.contains(value)) return;
    activeGroup_=value;
    saveMetadata();
    emit groupsChanged();
    rescan();               // link flags on every row change with the group
    setStatus(value.isEmpty()?QStringLiteral("No active group")
                             :QStringLiteral("Active group: %1").arg(value));
}

bool ProjectWorkspace::createGroup(const QString& groupName){
    const QString trimmed=groupName.trimmed();
    if(root_.isEmpty()){setStatus(QStringLiteral("Open or create a project first"));return false;}
    if(trimmed.isEmpty()){setStatus(QStringLiteral("A group needs a name"));return false;}
    if(groups_.contains(trimmed)){setStatus(QStringLiteral("\"%1\" already exists").arg(trimmed));return false;}
    groups_.insert(trimmed,QVariantMap{{QStringLiteral("datasets"),QStringList{}},
                                       {QStringLiteral("literature"),QStringList{}},
                                       {QStringLiteral("scripts"),QStringList{}}});
    activeGroup_=trimmed;
    saveMetadata();
    emit groupsChanged();
    rescan();
    setStatus(QStringLiteral("Created group \"%1\"").arg(trimmed));
    return true;
}

bool ProjectWorkspace::deleteGroup(const QString& groupName){
    if(!groups_.contains(groupName)) return false;
    groups_.remove(groupName);
    if(activeGroup_==groupName) activeGroup_=groups_.isEmpty()?QString():groupNames().first();
    saveMetadata();
    emit groupsChanged();
    rescan();
    // Only the grouping is gone. Every file is still exactly where it was.
    setStatus(QStringLiteral("Removed group \"%1\" - no files were deleted").arg(groupName));
    return true;
}

bool ProjectWorkspace::isLinked(const QString& path,const QString& kind) const{
    if(activeGroup_.isEmpty()) return false;
    return groups_.value(activeGroup_).toMap().value(kind).toStringList().contains(path,Qt::CaseInsensitive);
}

bool ProjectWorkspace::linkToActiveGroup(const QStringList& paths,const QString& kind){
    return linkToActiveGroup(paths,kind,true);
}

bool ProjectWorkspace::unlinkFromActiveGroup(const QStringList& paths,const QString& kind){
    return unlinkFromActiveGroup(paths,kind,true);
}

// rescanAfter exists because copyInto() and detachDataset() rescan themselves
// straight afterwards: adding one file to an adopted folder used to walk the
// whole folder twice.
bool ProjectWorkspace::linkToActiveGroup(const QStringList& paths,const QString& kind,
                                         bool rescanAfter){
    if(root_.isEmpty()||paths.isEmpty()) return false;
    if(activeGroup_.isEmpty()){
        // Linking without a group is what people actually mean by "remember
        // these together", so make one rather than refusing.
        createGroup(QStringLiteral("Default"));
        if(activeGroup_.isEmpty()) return false;
    }
    QVariantMap members=groups_.value(activeGroup_).toMap();
    QStringList current=members.value(kind).toStringList();
    int added=0;
    for(const QString& p:paths){
        const QString absolute=QFileInfo(p).absoluteFilePath();
        if(current.contains(absolute,Qt::CaseInsensitive)) continue;
        current.append(absolute);
        ++added;
    }
    members.insert(kind,current);
    groups_.insert(activeGroup_,members);
    saveMetadata();
    emit groupsChanged();
    if(rescanAfter) rescan();   // the rows carry a linked flag, so they must refresh
    if(added>0)
        setStatus(QStringLiteral("Linked %1 %2 to \"%3\"").arg(added).arg(kind,activeGroup_));
    return added>0;
}

bool ProjectWorkspace::unlinkFromActiveGroup(const QStringList& paths,const QString& kind,
                                            bool rescanAfter){
    if(activeGroup_.isEmpty()) return false;
    QVariantMap members=groups_.value(activeGroup_).toMap();
    QStringList current=members.value(kind).toStringList();
    int removed=0;
    for(const QString& p:paths){
        const QString absolute=QFileInfo(p).absoluteFilePath();
        for(int i=current.size()-1;i>=0;--i)
            if(current.at(i).compare(absolute,Qt::CaseInsensitive)==0){current.removeAt(i);++removed;}
    }
    members.insert(kind,current);
    groups_.insert(activeGroup_,members);
    saveMetadata();
    emit groupsChanged();
    if(rescanAfter) rescan();
    return removed>0;
}

QString ProjectWorkspace::revealFolder(const QString& which) const{
    if(root_.isEmpty()) return {};
    QString target=root_;
    if(which==QLatin1String("datasets")) target=datasetsDir();
    else if(which==QLatin1String("literature")) target=literatureDir();
    else if(which==QLatin1String("scripts")) target=scriptsDir();
    else if(which==QLatin1String("detached")) target=detachedDir();
    QDesktopServices::openUrl(QUrl::fromLocalFile(target));
    return target;
}
