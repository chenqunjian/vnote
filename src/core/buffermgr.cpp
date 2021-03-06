#include "buffermgr.h"

#include <QUrl>
#include <QDebug>

#include <notebook/node.h>
#include <buffer/filetypehelper.h>
#include <buffer/markdownbufferfactory.h>
#include <buffer/textbufferfactory.h>
#include <buffer/buffer.h>
#include <buffer/nodebufferprovider.h>
#include <buffer/filebufferprovider.h>
#include <utils/widgetutils.h>
#include "notebookmgr.h"
#include "vnotex.h"

#include "fileopenparameters.h"

using namespace vnotex;

BufferMgr::BufferMgr(QObject *p_parent)
    : QObject(p_parent)
{
}

BufferMgr::~BufferMgr()
{
    Q_ASSERT(m_buffers.isEmpty());
}

void BufferMgr::init()
{
    initBufferServer();
}

void BufferMgr::initBufferServer()
{
    m_bufferServer.reset(new NameBasedServer<IBufferFactory>);

    // Markdown.
    auto markdownFactory = QSharedPointer<MarkdownBufferFactory>::create();
    m_bufferServer->registerItem(FileTypeHelper::s_markdownFileType, markdownFactory);

    // Text.
    auto textFactory = QSharedPointer<TextBufferFactory>::create();
    m_bufferServer->registerItem(FileTypeHelper::s_textFileType, textFactory);
}

void BufferMgr::open(Node *p_node, const QSharedPointer<FileOpenParameters> &p_paras)
{
    if (!p_node) {
        return;
    }

    if (p_node->getType() == Node::Type::Folder) {
        return;
    }

    auto buffer = findBuffer(p_node);
    if (!buffer) {
        auto nodePath = p_node->fetchAbsolutePath();
        auto fileType = FileTypeHelper::fileType(nodePath);
        auto factory = m_bufferServer->getItem(fileType);
        if (!factory) {
            // No factory to open this file type.
            qInfo() << "File will be opened by system:" << nodePath;
            WidgetUtils::openUrlByDesktop(QUrl::fromLocalFile(nodePath));
            return;
        }

        BufferParameters paras;
        paras.m_provider.reset(new NodeBufferProvider(p_node));
        buffer = factory->createBuffer(paras, this);
        addBuffer(buffer);
    }

    Q_ASSERT(buffer);
    emit bufferRequested(buffer, p_paras);
}

void BufferMgr::open(const QString &p_filePath, const QSharedPointer<FileOpenParameters> &p_paras)
{
    if (p_filePath.isEmpty()) {
        return;
    }

    {
        QFileInfo info(p_filePath);
        if (!info.exists() || info.isDir()) {
            qWarning() << QString("failed to open file %1 exists:%2 isDir:%3").arg(p_filePath).arg(info.exists()).arg(info.isDir());
            return;
        }
    }

    // Check if it is an internal node or not.
    auto node = loadNodeByPath(p_filePath);
    if (node) {
        open(node.data(), p_paras);
        return;
    }

    auto buffer = findBuffer(p_filePath);
    if (!buffer) {
        // Open it as external file.
        auto fileType = FileTypeHelper::fileType(p_filePath);
        auto factory = m_bufferServer->getItem(fileType);
        if (!factory) {
            // No factory to open this file type.
            qInfo() << "File will be opened by system:" << p_filePath;
            WidgetUtils::openUrlByDesktop(QUrl::fromLocalFile(p_filePath));
            return;
        }

        BufferParameters paras;
        paras.m_provider.reset(new FileBufferProvider(p_filePath,
                                                      p_paras->m_nodeAttachedTo,
                                                      p_paras->m_readOnly));
        buffer = factory->createBuffer(paras, this);
        addBuffer(buffer);
    }

    Q_ASSERT(buffer);
    emit bufferRequested(buffer, p_paras);
}

Buffer *BufferMgr::findBuffer(const Node *p_node) const
{
    auto it = std::find_if(m_buffers.constBegin(),
                           m_buffers.constEnd(),
                           [p_node](const Buffer *p_buffer) {
                               return p_buffer->match(p_node);
                           });
    if (it != m_buffers.constEnd()) {
        return *it;
    }

    return nullptr;
}

Buffer *BufferMgr::findBuffer(const QString &p_filePath) const
{
    auto it = std::find_if(m_buffers.constBegin(),
                           m_buffers.constEnd(),
                           [p_filePath](const Buffer *p_buffer) {
                               return p_buffer->match(p_filePath);
                           });
    if (it != m_buffers.constEnd()) {
        return *it;
    }

    return nullptr;
}

void BufferMgr::addBuffer(Buffer *p_buffer)
{
    m_buffers.push_back(p_buffer);
    connect(p_buffer, &Buffer::attachedViewWindowEmpty,
            this, [this, p_buffer]() {
                qDebug() << "delete buffer without attached view window"
                         << p_buffer->getName();
                m_buffers.removeAll(p_buffer);
                p_buffer->close();
                p_buffer->deleteLater();
            });
}

QSharedPointer<Node> BufferMgr::loadNodeByPath(const QString &p_path)
{
    const auto &notebooks = VNoteX::getInst().getNotebookMgr().getNotebooks();
    for (const auto &nb : notebooks) {
        auto node = nb->loadNodeByPath(p_path);
        if (node) {
            return node;
        }
    }

    return nullptr;
}
