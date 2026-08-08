/*
 *  packetinfo.h
 *  Copyright 2003 Zaphod (dohpaz@users.sourceforge.net).
 *  Copyright 2003-2005, 2019 by the respective ShowEQ Developers
 *
 *  This file is part of ShowEQ.
 *  http://www.sourceforge.net/projects/seq
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef _PACKETINFO_H_
#define _PACKETINFO_H_

#include <cstdint>
#include <functional>
#include <vector>

#include <QObject>
#include <QList>
#include <QHash>
#include <QSet>
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QTextStream>

//----------------------------------------------------------------------
// forward declarations
class EQPacketOPCodeDB;
class EQPacketPayload;

//----------------------------------------------------------------------
// Enumerated types

// EQSizeCheckType - type of sizecheck that EQPacketPayload will perform
enum EQSizeCheckType
{
  SZC_None = 0,             // No payload size check
  SZC_Match,                // Exact size match
  SZC_Modulus,              // Payload is a modulus of the type size
  SZC_Variable = SZC_None,  // Variable length packet (effectively none)
};


//----------------------------------------------------------------------
// EQPacketTypeDB
class EQPacketTypeDB
{
 public:
  EQPacketTypeDB();
  ~EQPacketTypeDB();
  
  size_t size(const char* typeName) const;
  bool valid(const char* typeName) const;
  void list(void) const;

  // Backend gate-size ownership (see the ctor + struct_size_overrides()).
  // wasOverridden() is true when the linked backend explicitly declared this
  // type's size (so a SZC_Match gate on it is backend-owned, not an inherited
  // Live sizeof). hasOverrides() is true for any backend that ships overrides
  // (i.e. eql) — false for live/test, which legitimately use the compiled sizeof.
  bool wasOverridden(const QString& typeName) const;
  bool hasOverrides() const;

 protected:
  void addStruct(const char* typeName, size_t);

  QHash<QByteArray, size_t> m_typeSizeDict;
  QSet<QString>             m_overriddenTypes;
};

//----------------------------------------------------------------------
// PacketHandler — a decoded opcode payload delivered to a handler. This is
// the typed replacement for the old Qt string-SLOT dispatch: a handler is any
// callable, so it no longer has to be a QObject slot resolved by moc.
using PacketHandler = std::function<void(const uint8_t*, size_t, uint8_t)>;

//----------------------------------------------------------------------
// EQPacketDispatch — per-payload handler fan-out. Holds an ordered list of
// PacketHandlers; activate() fires them in registration order (the order that
// tier-2 goldens are sensitive to). Plain (non-QObject): handlers are typed
// callables registered via EQPacketStream::on(), so no moc/slot machinery is
// involved.
class EQPacketDispatch
{
 public:
  EQPacketDispatch();
  ~EQPacketDispatch();

  void activate(const uint8_t*, size_t, uint8_t);
  void add(PacketHandler handler);

 private:
  // disable copy constructor and operator=
  EQPacketDispatch(const EQPacketDispatch&);
  EQPacketDispatch& operator=(const EQPacketDispatch&);

  std::vector<PacketHandler> m_handlers;
};

//----------------------------------------------------------------------
// EQPacketPayload
class EQPacketPayload
{
 public:
  EQPacketPayload();
  ~EQPacketPayload();

  const QString& typeName() const;
  bool setType(const EQPacketTypeDB& db, const char* typeName);
  size_t typeSize() const;
  EQSizeCheckType sizeCheckType() const;
  void setSizeCheckType(EQSizeCheckType sizeCheckType);
  uint8_t dir() const;
  void setDir(uint8_t dir);

  bool match(const uint8_t* data, size_t size, uint8_t dir) const;

 protected:
  QString m_typeName;
  size_t m_typeSize;
  EQSizeCheckType m_sizeCheckType;
  uint8_t m_dir;
};

// Payload list typedef
typedef QList<EQPacketPayload*> EQPayloadList;
typedef QListIterator<EQPacketPayload*> EQPayloadListIterator;

inline const QString& EQPacketPayload::typeName() const
{
  return m_typeName;
}

inline size_t EQPacketPayload::typeSize() const
{
  return m_typeSize;
}

inline EQSizeCheckType EQPacketPayload::sizeCheckType() const
{
  return m_sizeCheckType;
}

inline void EQPacketPayload::setSizeCheckType(EQSizeCheckType sizeCheckType)
{
  m_sizeCheckType = sizeCheckType;
}

inline uint8_t EQPacketPayload::dir() const
{
  return m_dir;
}

inline void EQPacketPayload::setDir(uint8_t dir)
{
  m_dir = dir;
}

//----------------------------------------------------------------------
// EQPacketOPCode
class EQPacketOPCode : public EQPayloadList
{
 public:
  EQPacketOPCode();
  EQPacketOPCode(uint16_t, const QString& name);
  EQPacketOPCode(const EQPacketOPCode& opcode);
  ~EQPacketOPCode();

  void setOPCode(uint16_t opcode);
  uint16_t opcode(void) const;
  void setImplicitLen(uint16_t len);
  uint16_t implicitLen(void) const;
  void setName(const QString& name);
  const QString& name() const;
  void setUpdated(const QString& updated);
  const QString& updated(void) const;
  void addComment(const QString& comment);
  bool removeComment(const QString& comment);
  void clearComments(void);
  const QStringList& comments() const;
  EQPacketPayload* find(const uint8_t* data, size_t size, uint8_t dir) const;

 protected:
  uint16_t m_opcode;
  uint16_t m_implicitLen;
  QString m_name;
  QString m_updated;
  QStringList m_comments;
};

inline void EQPacketOPCode::setOPCode(uint16_t opcode)
{
  m_opcode = opcode;
}

inline uint16_t EQPacketOPCode::opcode(void) const
{
  return m_opcode;
}

inline void EQPacketOPCode::setImplicitLen(uint16_t len)
{
  m_implicitLen = len;
}

inline uint16_t EQPacketOPCode::implicitLen(void) const
{
  return m_implicitLen;
}

inline void EQPacketOPCode::setName(const QString& name)
{
  m_name = name;
}

inline const QString& EQPacketOPCode::name() const
{
  return m_name;
}

inline void EQPacketOPCode::setUpdated(const QString& updated)
{
  m_updated = updated;
}

inline const QString& EQPacketOPCode::updated(void) const
{
  return m_updated;
}

inline void EQPacketOPCode::addComment(const QString& comment)
{
  // append comment to the end of the list
  m_comments.append(comment);
}

inline bool EQPacketOPCode::removeComment(const QString& comment)
{
  // find the comment
  int index = m_comments.indexOf(comment);

  // was the comment found?
  if (index != -1)
  {
    // yes, remove it and return success
    m_comments.removeAt(index);
    return true;
  }

  // comment not found, return failure
  return false;
}

inline void EQPacketOPCode::clearComments(void)
{
  // clear the comments
  m_comments.clear();
}

inline const QStringList& EQPacketOPCode::comments() const
{
  // return a const version of the comments object
  return m_comments;
}

//----------------------------------------------------------------------
// EQPacketOPCodeDB
class EQPacketOPCodeDB
{
 public:
  ~EQPacketOPCodeDB();

  // `section` selects which array of opcodes.toml this DB loads: "zone" or
  // "world". One file carries both (they are separate id namespaces served by
  // separate DBs), so the section is the DB's identity, not a per-call option.
  explicit EQPacketOPCodeDB(const QString& section);

  bool load(const EQPacketTypeDB& typeDB, const QString& filename);
  bool save(const QString& filename);
  void list(void) const;

  // De-piggyback guard: warn (loudly, at load) for any MAPPED opcode whose
  // SZC_Match payload gate size still comes from the compiled Live sizeof
  // instead of an explicit backend size override. No-op unless the backend
  // ships overrides (eql). Turns the WearChange-class collision from a silent
  // mis-decode into a visible map-time error. See seq-backend-eql size_overrides().
  // Returns the number of flagged opcodes so --strict-gate-sizes can enforce it.
  int warnUndeclaredBackendGateSizes(const EQPacketTypeDB& typeDB) const;
  void clear(void);
  EQPacketOPCode* add(uint16_t opcode, const QString& opcodeName);
  EQPacketOPCode* edit(uint16_t opcode);
  EQPacketOPCode* edit(const QString& opcodeName);
  bool remove(uint16_t opcode);
  bool remove(const QString& opcodeName);
  bool move(uint16_t oldOPCode, uint16_t newOPCode);
  bool move(const QString& oldOPCodeName, const QString& newOPCodeName);
  const EQPacketOPCode* find(uint16_t opcode) const;
  const EQPacketOPCode* find(const QString& opcodeName) const;
  const QHash<int, EQPacketOPCode*> opcodes() const;

 protected:
  QHash<int, EQPacketOPCode*> m_opcodes;
  QHash<QString, EQPacketOPCode*> m_opcodesByName;
  // Which opcodes.toml array this DB owns ("zone" / "world"). One file carries
  // both, so this is the DB's identity rather than a per-load option.
  QString m_section;
};

inline void EQPacketOPCodeDB::clear(void)
{
  qDeleteAll(m_opcodes);
  m_opcodes.clear();
  m_opcodesByName.clear();
}

inline EQPacketOPCode* EQPacketOPCodeDB::edit(uint16_t opcode)
{
  // attempt to find the opcode object
  return m_opcodes.value(opcode, nullptr);
}

inline EQPacketOPCode* EQPacketOPCodeDB::edit(const QString& name)
{
  // attempt to find the opcode object
  return m_opcodesByName.value(name, nullptr);
}

inline const EQPacketOPCode* EQPacketOPCodeDB::find(uint16_t opcode) const
{
  // attempt to find the opcode object
  return m_opcodes.value(opcode, nullptr);
}

inline const EQPacketOPCode* EQPacketOPCodeDB::find(const QString& opcode) const
{
  // attempt to find the opcode object
  return m_opcodesByName.value(opcode, nullptr);
}

inline const QHash<int, EQPacketOPCode*> EQPacketOPCodeDB::opcodes() const
{
  return m_opcodes;
}

#endif // _PACKETINFO_H_
