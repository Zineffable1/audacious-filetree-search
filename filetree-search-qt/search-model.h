/*
 * search-model.h
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

#ifndef SEARCHMODEL_H
#define SEARCHMODEL_H

#include <QAbstractItemModel>

#include <libaudcore/audstrings.h>
#include <libaudcore/i18n.h>
#include <libaudcore/multihash.h>
#include <libaudcore/playlist.h>

enum class SearchField {
    Genre,
    Artist,
    Album,
    HiddenAlbum,
    Title,
    count
};

static constexpr aud::array<SearchField, const char *> start_tags =
    {"", "<b>", "<i>", "<i>", ""};
static constexpr aud::array<SearchField, const char *> end_tags =
    {"", "</b>", "</i>", "</i>", ""};

static inline const char * parent_prefix (SearchField parent)
{
    return (parent == SearchField::Album || parent ==
     SearchField::HiddenAlbum) ? _("on") : _("by");
}

struct Key
{
    SearchField field;
    String name;

    bool operator== (const Key & b) const
        { return field == b.field && name == b.name; }
    unsigned hash () const
        { return (unsigned) field + name.hash (); }
};

struct Item
{
    SearchField field;
    String name, folded;
    Item * parent;
    SimpleHash<Key, Item> children;
    Index<int> matches;
    bool m_search_visible = true;

    Item (SearchField field, const String & name, Item * parent) :
        field (field),
        name (name),
        folded (str_tolower_utf8 (name)),
        parent (parent) {}

    Item (Item &&) = default;
    Item & operator= (Item &&) = default;
};

class SearchModel : public QAbstractItemModel
{
public:
    int num_items () const { return m_root_items.len (); }
    const Item * item_at_index (const QModelIndex & index) const;
    int num_hidden_items () const { return m_hidden_items; }

    void update ();
    void destroy_database ();
    void create_database (Playlist playlist, const String & base_path = String());
    void do_search (const Index<String> & terms);

    int rowCount (const QModelIndex & parent) const override;
    int columnCount (const QModelIndex & parent) const override { return 1; }
    QVariant data (const QModelIndex & index, int role) const override;
    QModelIndex parent (const QModelIndex & index) const override;
    QModelIndex index (int row, int column, const QModelIndex & parent = QModelIndex ()) const override;
    bool hasChildren (const QModelIndex & parent = QModelIndex ()) const override;

    Qt::ItemFlags flags (const QModelIndex & index) const override
    {
        if (index.isValid ())
            return Qt::ItemIsSelectable | Qt::ItemIsDragEnabled | Qt::ItemIsEnabled;
        else
            return Qt::ItemIsSelectable | Qt::ItemIsEnabled;
    }

    QStringList mimeTypes () const override
    {
        return QStringList ("text/uri-list");
    }

    QMimeData * mimeData (const QModelIndexList & indexes) const override;

private:
    void add_to_database (int entry, std::initializer_list<Key> keys);
    void build_root_items();

    Playlist m_playlist;
    SimpleHash<Key, Item> m_database;
    Index<Item *> m_root_items;
    int m_hidden_items = 0;
};

#endif // SEARCHMODEL_H