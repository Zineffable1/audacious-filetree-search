/*
 * search-model.cc
 * Copyright 2011-2019 John Lindgren and René J.V. Bertin
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the documentation
 *    provided with the distribution.
 *
 * This software is provided "as is" and without any warranty, express or
 * implied. In no event shall the authors be liable for any damages arising from
 * the use of this software.
 */

#include "search-model.h"

#include <QMimeData>
#include <QSet>
#include <QUrl>

#include <libaudcore/i18n.h>

static QString create_item_label (const Item & item)
{
    QString string;
    
    // Add folder icon for directories (non-Title items)
    if (item.field != SearchField::Title)
        string += "📁 ";
    
    string += start_tags[item.field];

    // Only uppercase if it's actually a Genre field (from old search, not folder names)
    if (item.field == SearchField::Genre && item.parent == nullptr)
        string += QString (str_toupper_utf8 (item.name)).toHtmlEscaped ();
    else
        string += QString (item.name).toHtmlEscaped ();

    string += end_tags[item.field];

    // Build the extra info first to see if we need the br tag
    QString extra_info;
    bool has_extra = false;

    if (item.field != SearchField::Title)
    {
        // Only show song count for items that actually have songs
        if (item.matches.len() > 0)
        {
            extra_info += str_printf (dngettext (PACKAGE, "%d song", "%d songs",
             item.matches.len ()), item.matches.len ());
            has_extra = true;

            if (item.field == SearchField::Genre || item.parent)
                extra_info += ' ';
        }
    }

    if (item.field == SearchField::Genre)
    {
        // Only show "of this genre" if it actually has matches (not a folder)
        if (item.matches.len() > 0)
        {
            extra_info += _("of this genre");
            has_extra = true;
        }
    }
    else if (item.parent)
    {
        auto parent = (item.parent->parent ? item.parent->parent : item.parent);

        extra_info += parent_prefix (parent->field);
        extra_info += ' ';
        extra_info += start_tags[parent->field];
        extra_info += QString (parent->name).toHtmlEscaped ();
        extra_info += end_tags[parent->field];
        has_extra = true;
    }

    // Only add br and extra info if there's something to show
    if (has_extra)
    {
#ifdef Q_OS_MAC
        string += "<br>";
        string += extra_info;
#else
        string += "<br><small>";
        string += extra_info;
        string += "</small>";
#endif
    }

    return string;
}

const Item * SearchModel::item_at_index (const QModelIndex & index) const
{
    if (!index.isValid())
        return nullptr;
    
    return static_cast<Item *>(index.internalPointer());
}

QVariant SearchModel::data (const QModelIndex & index, int role) const
{
    if (role == Qt::DisplayRole)
    {
        const Item * item = item_at_index(index);
        if (!item)
            return QVariant ();

        return create_item_label (* item);
    }

    return QVariant ();
}

int SearchModel::rowCount (const QModelIndex & parent) const
{
    if (!parent.isValid())
        return m_root_items.len();
    
    const Item * item = item_at_index(parent);
    if (!item)
        return 0;
    
    // Count only visible children
    int count = 0;
    const_cast<SimpleHash<Key, Item>&>(item->children).iterate([&](const Key &, Item & child) {
        if (child.m_search_visible)
            count++;
    });
    return count;
}

QModelIndex SearchModel::parent (const QModelIndex & index) const
{
    if (!index.isValid())
        return QModelIndex();
    
    const Item * item = static_cast<Item *>(index.internalPointer());
    if (!item || !item->parent)
        return QModelIndex();
    
    Item * parent = item->parent;
    if (!parent->parent)
    {
        for (int row = 0; row < m_root_items.len(); row++)
        {
            if (m_root_items[row] == parent)
                return createIndex(row, 0, parent);
        }
    }
    else
    {
        Item * grandparent = parent->parent;
        int row = 0;
        const_cast<SimpleHash<Key, Item>&>(grandparent->children).iterate([&](const Key &, Item & child) {
            if (&child == parent)
                return;
            row++;
        });
        return createIndex(row, 0, parent);
    }
    
    return QModelIndex();
}

QModelIndex SearchModel::index (int row, int column, const QModelIndex & parent) const
{
    if (column != 0)
        return QModelIndex();
    
    if (!parent.isValid())
    {
        if (row < 0 || row >= m_root_items.len())
            return QModelIndex();
        return createIndex(row, 0, m_root_items[row]);
    }
    
    const Item * parent_item = item_at_index(parent);
    if (!parent_item)
        return QModelIndex();
    
    // Get visible children in sorted order
    Index<Item *> children;
    const_cast<SimpleHash<Key, Item>&>(parent_item->children).iterate([&](const Key &, Item & item) {
        if (item.m_search_visible)
            children.append(&item);
    });
    
    children.sort([](Item * const & a, Item * const & b) {
        return str_compare(a->name, b->name) > 0;
    });
    
    if (row < 0 || row >= (int)children.len())
        return QModelIndex();
    
    return createIndex(row, 0, children[row]);
}

bool SearchModel::hasChildren (const QModelIndex & parent) const
{
    if (!parent.isValid())
        return m_root_items.len() > 0;
    
    const Item * item = item_at_index(parent);
    if (!item)
        return false;
    
    // Check if there are any visible children
    bool has_visible = false;
    const_cast<SimpleHash<Key, Item>&>(item->children).iterate([&](const Key &, Item & child) {
        if (child.m_search_visible)
            has_visible = true;
    });
    return has_visible;
}

QMimeData * SearchModel::mimeData (const QModelIndexList & indexes) const
{
    m_playlist.select_all (false);

    QList<QUrl> urls;
    QSet<int> seen_entries;  // Track unique entries to avoid duplicates
    
    // Recursive function to collect all files in folders
    std::function<void(const Item *)> collect_files = [&](const Item * folder) {
        // Get children and sort them
        Index<Item *> children;
        const_cast<SimpleHash<Key, Item>&>(folder->children).iterate([&](const Key &, Item & child) {
            children.append(&child);
        });
        
        children.sort([](Item * const & a, Item * const & b) {
            return str_compare(a->name, b->name) > 0;
        });
        
        // Process in sorted order
        for (auto child : children)
        {
            if (child->field == SearchField::Title && child->matches.len() > 0)
            {
                // This is a file - add each entry if not already seen
                for (int entry : child->matches)
                {
                    if (!seen_entries.contains(entry))
                    {
                        seen_entries.insert(entry);
                        urls.append (QString (m_playlist.entry_filename (entry)));
                        m_playlist.select_entry (entry, true);
                    }
                }
            }
            else if (child->children.n_items() > 0)
            {
                // This is a subfolder, recurse
                collect_files(child);
            }
        }
    };
    
    for (auto & index : indexes)
    {
        const Item * item = item_at_index(index);
        if (!item)
            continue;

        // Check if this is a file or a folder
        if (item->field == SearchField::Title && item->matches.len() > 0)
        {
            // This is a single file - add it directly
            for (int entry : item->matches)
            {
                if (!seen_entries.contains(entry))
                {
                    seen_entries.insert(entry);
                    urls.append (QString (m_playlist.entry_filename (entry)));
                    m_playlist.select_entry (entry, true);
                }
            }
        }
        else if (item->children.n_items() > 0)
        {
            // This is a folder - recursively collect all files
            collect_files(item);
        }
    }

    m_playlist.cache_selected ();

    auto data = new QMimeData;
    data->setUrls (urls);
    return data;
}

void SearchModel::update ()
{
    beginResetModel();
    build_root_items();
    endResetModel();
}

void SearchModel::build_root_items()
{
    m_root_items.clear();
    m_database.iterate([&](const Key &, Item & item) {
        m_root_items.append(&item);
    });
    
    // Sort alphabetically
    m_root_items.sort([](Item * const & a, Item * const & b) {
        return str_compare(a->name, b->name) > 0;
    });
}

void SearchModel::destroy_database ()
{
    m_playlist = Playlist ();
    m_root_items.clear ();
    m_hidden_items = 0;
    m_database.clear ();
}

void SearchModel::add_to_database (int entry, std::initializer_list<Key> keys)
{
    Item * parent = nullptr;
    auto hash = & m_database;

    for (auto & key : keys)
    {
        if (! key.name)
            continue;

        Item * item = hash->lookup (key);
        if (! item)
            item = hash->add (key, Item (key.field, key.name, parent));

        item->matches.append (entry);

        parent = item;
        hash = & item->children;
    }
}

void SearchModel::create_database (Playlist playlist, const String & base_path)
{
    destroy_database ();

    int entries = playlist.n_entries ();
    
    // Convert base_path (URI) to proper format
    QString base_dir;
    if (base_path)
    {
        base_dir = QString::fromUtf8((const char *)base_path);
        base_dir = QUrl::fromPercentEncoding(base_dir.toUtf8());
        
        // Remove file:// prefix if present
        if (base_dir.startsWith("file://"))
            base_dir = base_dir.mid(7);
        
        // Ensure it doesn't end with /
        if (base_dir.endsWith("/"))
            base_dir.chop(1);
    }

    for (int e = 0; e < entries; e ++)
    {
       String filename = playlist.entry_filename(e);
if (! filename)
    continue;

// convert to QString and decode spaces
QString fullpath = QString::fromUtf8((const char *)filename);
fullpath = QUrl::fromPercentEncoding(fullpath.toUtf8());

// Remove file:// prefix if present
if (fullpath.startsWith("file://"))
    fullpath = fullpath.mid(7);

// Strip the base directory path
if (!base_dir.isEmpty() && fullpath.startsWith(base_dir + "/"))
{
    fullpath = fullpath.mid(base_dir.length() + 1);
}

QStringList parts = fullpath.split("/", Qt::SkipEmptyParts);

Item * parent = nullptr;
auto hash = &m_database;

for (int i = 0; i < parts.size(); ++i) {
    QString part = parts[i];
    String partStr(part.toUtf8());

    // last component = file, others = folder
    SearchField field = (i == parts.size() - 1) ? SearchField::Title : SearchField::Genre;

    Key key { field, partStr };

    Item * item = hash->lookup(key);
    if (!item)
        item = hash->add(key, Item(field, partStr, parent));

    // only append matches to the file node
    if (field == SearchField::Title)
        item->matches.append(e);

    parent = item;
    hash = &item->children;
}


    }

    m_playlist = playlist;
}


static void search_recurse (SimpleHash<Key, Item> & domain,
 const Index<String> & terms, int mask, Index<const Item *> & results)
{
    domain.iterate ([&] (const Key & key, Item & item)
    {
        int count = terms.len ();
        int new_mask = mask;

        for (int t = 0, bit = 1; t < count; t ++, bit <<= 1)
        {
            if (! (new_mask & bit))
                continue; /* skip term if it is already found */

            if (strstr (item.folded, terms[t]))
                new_mask &= ~bit; /* we found it */
            else if (! item.children.n_items ())
                break; /* quit early if there are no children to search */
        }

        /* adding an item with exactly one child is redundant, so avoid it */
        if (! new_mask && item.children.n_items () != 1 &&
         item.field != SearchField::HiddenAlbum)
            results.append (& item);

        search_recurse (item.children, terms, new_mask, results);
    });
}

static int item_compare (const Item * const & a, const Item * const & b)
{
    if (a->field < b->field)
        return -1;
    if (a->field > b->field)
        return 1;

    int val = str_compare (a->name, b->name);
    if (val)
        return val;

    if (a->parent)
        return b->parent ? item_compare (a->parent, b->parent) : 1;
    else
        return b->parent ? -1 : 0;
}

void SearchModel::do_search (const Index<String> & terms)
{
    m_hidden_items = 0;
    
    // Mark all items as matching or not based on search
    std::function<bool(Item *)> mark_matches = [&](Item * item) -> bool {
        bool item_matches = false;
        bool child_matches = false;
        
        // Check if this item matches search terms
        for (int t = 0; t < terms.len(); t++)
        {
            if (strstr(item->folded, terms[t]))
            {
                item_matches = true;
                break;
            }
        }
        
        // Check if any children match
        const_cast<SimpleHash<Key, Item>&>(item->children).iterate([&](const Key &, Item & child) {
            if (mark_matches(&child))
                child_matches = true;
        });
        
        // Item should be visible if it matches OR has matching children
        item->m_search_visible = (item_matches || child_matches || terms.len() == 0);
        return item->m_search_visible;
    };
    
    // Mark all items
    m_database.iterate([&](const Key &, Item & item) {
        mark_matches(&item);
    });
    
    // Rebuild root items with only visible ones
    build_root_items();
}