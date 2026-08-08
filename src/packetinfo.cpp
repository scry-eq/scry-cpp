/*
 *  packetinfo.cpp
 *  Copyright 2003-2004,2007 Zaphod (dohpaz@users.sourceforge.net).
 *  Copyright 2005-2007, 2019 by the respective ShowEQ Developers
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

#include <cstdio>

#include <QObject>
#include <QMetaObject>
#include <QFile>
#include <QTextStream>
#include <QByteArray>
#include <QXmlStreamReader>

#include "toml.hpp"

#include <map>
#include <string>

#include "packetinfo.h"
#include "packetcommon.h"
#include "everquest.h"
#include "diagnosticmessages.h"
#include "seq-bridge-cxx/lib.h"


//----------------------------------------------------------------------
// Macros

// this define is used to diagnose problems with packet dispatch
// #define  PACKET_DISPATCH_DIAG 1


//----------------------------------------------------------------------
// EQPacketTypeDB
EQPacketTypeDB::EQPacketTypeDB()
  : m_typeSizeDict()
{
  // define the convenience macro used in the generated file
#define AddStruct(typeName) addStruct(#typeName, sizeof(typeName))

  // include the generated file
#include "s_everquest.h"

  // undefine the convenience macro
#undef AddStruct

  // these we add manually to handle strings and octet streams
  addStruct("char", sizeof(char));
  addStruct("uint8_t", sizeof(uint8_t));
  addStruct("none", 0);
  addStruct("unknown", 0);

  // Backend-sourced size overrides. The size table above is built from the
  // daemon's compiled (Live) everquest.h sizeof (s_everquest.h); the linked
  // Rust backend declares the authoritative size for any payload whose wire
  // diverges (e.g. eql's 24B considerStruct vs Live's 32B). Applied last so
  // they win over the sizeof entries. This is why SZC_Match validates against
  // the backend's size, not a hardcoded daemon sizeof; live/test return an
  // empty list, so this is a no-op there.
  for (const auto& s : seq::rust::struct_size_overrides())
  {
    std::string name(s.name.data(), s.name.size());
    addStruct(name.c_str(), (size_t)s.size);
    // Record that this type's size is backend-owned, so the load-time gate-size
    // audit (warnUndeclaredBackendGateSizes) can tell a declared eql size apart
    // from a silently-inherited Live sizeof.
    m_overriddenTypes.insert(QString::fromLatin1(name.c_str()));
  }
}

bool EQPacketTypeDB::wasOverridden(const QString& typeName) const
{
  return m_overriddenTypes.contains(typeName);
}

bool EQPacketTypeDB::hasOverrides() const
{
  return !m_overriddenTypes.isEmpty();
}

EQPacketTypeDB::~EQPacketTypeDB()
{
}

size_t EQPacketTypeDB::size(const char* typeName) const
{
  // attempt to find the item in the type size dictionary
  size_t size = m_typeSizeDict.value(typeName, 0);

  return size;
}

bool EQPacketTypeDB::valid(const char* typeName) const
{
  // attempt to find the item in the type size dictionary
  size_t size = m_typeSizeDict.value(typeName, 0);

  return (size != 0);
}

void EQPacketTypeDB::list(void) const
{
  seqInfo("EQPacketTypeDB contains %d types (in %d buckets)",
	  m_typeSizeDict.count(), m_typeSizeDict.size());

  QHashIterator<QByteArray, size_t> it(m_typeSizeDict);

  while (it.hasNext())
  {
    it.next();
    seqInfo("\t%s = %d", it.key().data(), it.value());
  }
}

void EQPacketTypeDB::addStruct(const char* typeName, size_t size)
{
  m_typeSizeDict.insert(typeName, size);
}

//----------------------------------------------------------------------
// EQPacketDispatch
EQPacketDispatch::EQPacketDispatch()
{
}

EQPacketDispatch::~EQPacketDispatch()
{
}

void EQPacketDispatch::activate(const uint8_t* data, size_t len, uint8_t dir)
{
  // Fire handlers in registration order — goldens depend on it.
  for (const PacketHandler& h : m_handlers)
    h(data, len, dir);
}

void EQPacketDispatch::add(PacketHandler handler)
{
  m_handlers.push_back(std::move(handler));
}

//----------------------------------------------------------------------
// EQPacketPayload
EQPacketPayload::EQPacketPayload()
  : m_typeSize(0),
    m_sizeCheckType(SZC_None),
    m_dir(0x00)
{
}

EQPacketPayload::~EQPacketPayload()
{
}

bool EQPacketPayload::setType(const EQPacketTypeDB& db, 
			      const char* typeName)
{
  // first, check that it is a valid type
  if (!db.valid(typeName))
    return false;

  // valid type, ok, use it
  m_typeName = typeName;

  // get the types size
  m_typeSize = db.size(typeName);

  return true;
}

bool EQPacketPayload::match(const uint8_t* data, size_t size, 
			    uint8_t dir) const
{
  switch(m_sizeCheckType)
  {
  case SZC_None:
    return ((m_dir & dir) != 0);
  case SZC_Match:
    return (((m_dir & dir) != 0) &&
	    (m_typeSize == size));
  case SZC_Modulus:
    return (((m_dir & dir) != 0) &&
	    ((size % m_typeSize) == 0));
  default:
    break;
  }

  return false;
}


//----------------------------------------------------------------------
// EQPacketOPCode
EQPacketOPCode::EQPacketOPCode()
  : m_opcode(0),
    m_implicitLen(0)
{
}

EQPacketOPCode::EQPacketOPCode(uint16_t opcode, const QString& name)
  : m_opcode(opcode),
    m_implicitLen(0),
    m_name(name)
{
}

EQPacketOPCode::EQPacketOPCode(const EQPacketOPCode& opcode)
  : m_opcode(opcode.m_opcode),
    m_implicitLen(opcode.m_implicitLen),
    m_name(opcode.m_name),
    m_updated(opcode.m_updated)
{
}

EQPacketOPCode::~EQPacketOPCode()
{
    qDeleteAll(*this);
    clear();
}

EQPacketPayload* EQPacketOPCode::find(const uint8_t* data, size_t size, uint8_t dir) const
{
  EQPacketPayload* payload;

  // iterate over the payloads until a matching one is found
  EQPayloadListIterator it(*this);
  while (it.hasNext())
  {
    payload = it.next();
    if (!payload)
        break;
    // if a match is found, return it.
    if (payload->match(data, size, dir))
      return payload;
  }

  // no matches, return 0
  return 0;
}


//----------------------------------------------------------------------
// EQPacketOPCodeDB
EQPacketOPCodeDB::EQPacketOPCodeDB(const QString& section)
  : m_opcodes(), m_section(section)
{
}

EQPacketOPCodeDB::~EQPacketOPCodeDB()
{
    while(!m_opcodesByName.isEmpty()) {
        remove(m_opcodesByName.begin().key());
    }
}

bool EQPacketOPCodeDB::load(const EQPacketTypeDB& typeDB,
			    const QString& filename)
{
  // opcodes.toml is read DIRECTLY — there is no generated XML any more.
  // It used to be TOML -> tools/toml_to_xml.py -> {zone,world}opcodes.xml ->
  // here, which gave the same data two on-disk representations and two ways to
  // desync: the CMake regeneration target could serve a stale XML (a mapped
  // world table once read as all-ffff that way), and a TOML syntax error left
  // the OLD xml in place so check.sh passed against data nobody had edited.
  // One file, one reader, neither failure is expressible.
  toml::table doc;
  try
  {
    doc = toml::parse_file(filename.toStdString());
  }
  catch (const toml::parse_error& err)
  {
    // Loud and specific: a malformed table means EVERY opcode is unmapped, and
    // the downstream symptom (nothing decodes) looks nothing like a parse error.
    const auto& src = err.source();
    seqWarn("EQPacketOPCodeDB::load: %s:%u:%u: %s",
            filename.toLatin1().data(),
            src.begin.line, src.begin.column, err.description().data());
    return false;
  }

  // `[[zone]]` / `[[world]]` are separate id namespaces and live in separate
  // DBs, so one file serves both: each DB loads only the array it was asked
  // for. The caller passes the section (see EQPacket's ctor).
  const auto* arr = doc.get_as<toml::array>(m_section.toStdString());
  if (!arr)
  {
    // An absent section is empty, not broken — a target may legitimately map
    // no world opcodes. An empty DB is still a successful load.
    return true;
  }

  for (const auto& node : *arr)
  {
    const auto* entry = node.as_table();
    if (!entry)
      continue;

    const auto idStr = entry->get_as<std::string>("id");
    const auto nameStr = entry->get_as<std::string>("name");
    if (!idStr || !nameStr)
    {
      seqWarn("EQPacketOPCodeDB::load: %s entry missing id or name",
              m_section.toLatin1().data());
      return false;
    }

    bool ok = false;
    const QString qid = QString::fromStdString(**idStr);
    const uint16_t opcode = qid.toUShort(&ok, 16);
    if (!ok)
    {
      seqWarn("EQPacketOPCodeDB::load: opcode '%s' failed to convert to uint16_t",
              qid.toLatin1().data());
      return false;
    }

    // "ffff" rows (unmapped this patch) are kept, NOT skipped. They all collide
    // on the 0xffff id key, but `dispatchFor` resolves BY NAME — so dropping
    // them would turn every wire() against an unmapped opcode into "Unknown
    // opcode" instead of a successful bind, and eql wires plenty of those.
    // Behaviour here is identical to the XML reader's on purpose.
    EQPacketOPCode* currentOPCode =
      add(opcode, QString::fromStdString(**nameStr));
    if (!currentOPCode)
    {
      seqWarn("Failed to add opcode %04x", opcode);
      return false;
    }

    if (const auto v = entry->get_as<std::string>("updated"))
      currentOPCode->setUpdated(QString::fromStdString(**v));
    if (const auto v = entry->get_as<std::string>("implicitlen"))
      currentOPCode->setImplicitLen(QString::fromStdString(**v).toUShort());
    if (const auto v = entry->get_as<std::string>("comment"))
      currentOPCode->addComment(QString::fromStdString(**v));

    // priority / priority_note are patch-day triage metadata and are
    // deliberately load-ignored, exactly as the XML attributes were.

    const auto* payloads = entry->get_as<toml::array>("payloads");
    if (!payloads)
      continue;

    for (const auto& pnode : *payloads)
    {
      const auto* p = pnode.as_table();
      if (!p)
        continue;

      auto* currentPayload = new EQPacketPayload();
      currentOPCode->append(currentPayload);

      if (const auto v = p->get_as<std::string>("dir"))
      {
        const std::string& dir = **v;
        if (dir == "both")
          currentPayload->setDir(DIR_Client | DIR_Server);
        else if (dir == "server")
          currentPayload->setDir(DIR_Server);
        else if (dir == "client")
          currentPayload->setDir(DIR_Client);
      }

      if (const auto v = p->get_as<std::string>("typename"))
      {
        const std::string& typeName = **v;
        if (!typeName.empty())
        {
          if (!currentPayload->setType(typeDB, typeName.c_str()))
            seqWarn("Unknown payload typename '%s' for opcode '%04x'",
                    typeName.c_str(), currentOPCode->opcode());
        }
      }

      if (const auto v = p->get_as<std::string>("sizechecktype"))
      {
        const std::string& szt = **v;
        if (szt.empty() || szt == "none")
          currentPayload->setSizeCheckType(SZC_None);
        else if (szt == "match")
          currentPayload->setSizeCheckType(SZC_Match);
        else if (szt == "modulus")
          currentPayload->setSizeCheckType(SZC_Modulus);
      }
    }
  }

  return true;
}

bool EQPacketOPCodeDB::save(const QString& filename)
{
  // create QFile object
  QFile file(filename);

  // open the file for write only
  if (!file.open(QIODevice::WriteOnly))
    return false;

  // create a QTextStream object on the QFile object
  QTextStream out(&file);

  // set the output encoding to be UTF8
#if (QT_VERSION >= QT_VERSION_CHECK(6,0,0))
  out.setEncoding(QStringConverter::Utf8);
#else
  out.setCodec("UTF-8");
#endif

  // set the number output to be left justified decimal
  out.setIntegerBase(10);
  out.setFieldAlignment(QTextStream::AlignLeft);

  // print document header
  out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" << Qt::endl
      << "<!DOCTYPE seqopcodes SYSTEM \"seqopcodes.dtd\">" << Qt::endl
      << "<seqopcodes>" << Qt::endl;

  // set initial indent
  QString indent = "    ";

  EQPacketOPCode* currentOPCode;
  EQPacketPayload* currentPayload;

  typedef std::map<long, EQPacketOPCode*> OrderedMap;
  OrderedMap orderedOPCodes;

  // iterate over all the opcodes, inserting them into the ordered map
  QHashIterator<int, EQPacketOPCode*> it(m_opcodes);
  while (it.hasNext())
  {
    it.next();
    if (!it.value())
        break;
    currentOPCode = it.value();
    // insert into the ordered opcode map
    orderedOPCodes.insert(OrderedMap::value_type(currentOPCode->opcode(), 
						 currentOPCode));

  }

  OrderedMap::iterator oit;
  QString opcodeString;
  static const char* dirStrs[] = { "client", "server", "both", };
  static const char* sztStrs[] = { "none", "match", "modulus", };

  // iterate over the ordered opcode map
  for (oit = orderedOPCodes.begin(); oit != orderedOPCodes.end(); ++oit)
  {
    currentOPCode = oit->second;

    // output the current opcode
    opcodeString = QString::asprintf("%04x", currentOPCode->opcode());
    out << indent << "<opcode id=\"" << opcodeString << "\" name=\""
	<< currentOPCode->name() << "\"";
    if (currentOPCode->implicitLen())
      out << " implicitlen=\"" << currentOPCode->implicitLen() << "\"";
    if (!currentOPCode->updated().isEmpty())
      out << " updated=\"" << currentOPCode->updated() << "\"";
    out << ">" << Qt::endl;

    // increase the indent
    indent += "    ";

    // output the comments
    QStringList comments = currentOPCode->comments();
    for (QStringList::Iterator cit = comments.begin(); 
	 cit != comments.end(); ++cit)
      out << indent << "<comment>" << *cit << "</comment>" << Qt::endl;

    QByteArray dirStr;
    QByteArray sztStr;

    // iterate over the payloads
    QListIterator<EQPacketPayload*> pit(*currentOPCode);
    while (pit.hasNext())
    {
      currentPayload = pit.next();
      if (!currentPayload)
          break;

      // output the payload
      out << indent << "<payload dir=\"" << dirStrs[currentPayload->dir()-1]
	  << "\" typename=\"" << currentPayload->typeName() 
	  << "\" sizechecktype=\""
	  << sztStrs[currentPayload->sizeCheckType()]
	  << "\"/>" << Qt::endl;
    }

    // decrease the indent
    indent.remove(0, 4);

    // close the opcode entity
    out << indent << "</opcode>" << Qt::endl;
  }

  // output closing entity
  out << "</seqopcodes>" << Qt::endl;

  return true;
}

EQPacketOPCode* EQPacketOPCodeDB::add(uint16_t opcode, const QString& name)
{
  // Create the new opcode object
  EQPacketOPCode* newOPCode = new EQPacketOPCode(opcode, name);

  // insert the opcode into the opcode table
  m_opcodes.insert(opcode, newOPCode);

  // insert the object into the opcode by name table
  m_opcodesByName.insert(name, newOPCode);

  // return the opcode object
  return newOPCode;
}

int EQPacketOPCodeDB::warnUndeclaredBackendGateSizes(const EQPacketTypeDB& typeDB) const
{
  // Only backends that ship size overrides (eql) own their gate sizes; live/test
  // legitimately gate on the compiled everquest.h sizeof, so this is a no-op there.
  if (!typeDB.hasOverrides())
    return 0;

  int flagged = 0;
  QHashIterator<int, EQPacketOPCode*> it(m_opcodes);
  while (it.hasNext())
  {
    it.next();
    EQPacketOPCode* op = it.value();
    if (!op || op->opcode() == 0xffff)   // unmapped: no wire packets → no live gate yet
      continue;

    QListIterator<EQPacketPayload*> pit(*op);
    while (pit.hasNext())
    {
      const EQPacketPayload* pl = pit.next();
      if (!pl || pl->sizeCheckType() != SZC_Match)
        continue;   // SZC_None / other checks don't gate on an exact struct size

      const QString& tn = pl->typeName();
      if (tn == "uint8_t" || tn == "char" || tn == "none" || tn == "unknown")
        continue;   // primitive / sizeless payloads carry no Live struct

      if (!typeDB.wasOverridden(tn))
      {
        seqWarn("BACKEND GATE-SIZE: %s (0x%04x) SZC_Match gates on the compiled Live "
                "sizeof(%s)=%zu — declare its size in seq-backend-eql size_overrides() so the "
                "eql gate is backend-owned (WearChange-class collision guard; see OPCODES_LEGENDS.md).",
                op->name().toLatin1().constData(), op->opcode(),
                tn.toLatin1().constData(), typeDB.size(tn.toLatin1().constData()));
        ++flagged;
      }
    }
  }

  if (flagged)
    seqWarn("BACKEND GATE-SIZE: %d mapped SZC_Match opcode(s) still inherit a Live sizeof instead "
            "of a backend-owned size — each is a silent-mis-decode landmine if that Live struct "
            "diverges or collides. Add explicit size_overrides() entries.",
            flagged);
  return flagged;
}

void EQPacketOPCodeDB::list(void) const
{
  seqInfo("EQPacketOPCodeDB contains %d opcodes (in %d buckets)",
	  m_opcodes.count(), m_opcodes.size());

  EQPacketOPCode* current;
  EQPacketPayload* currentPayload;

  // iterate over all the opcodes
  QHashIterator<int, EQPacketOPCode*> it(m_opcodes);
  while (it.hasNext())
  {
    it.next();
    if (!it.value())
        break;
    current = it.value();
    fprintf(stderr, "\tkey=%04x opcode=%04x",
	    it.key(), current->opcode());
    if (!current->name().isNull())
      fprintf(stderr, " name='%s'", current->name().toLatin1().data());

    if (current->implicitLen())
      fprintf(stderr, " implicitlen='%d'", current->implicitLen());

    if (!current->updated().isNull())
      fprintf(stderr, " updated='%s'", current->updated().toLatin1().data());

    fputc('\n', stderr);

    QStringList comments = current->comments();

    fprintf(stderr, "\t\t%lld comment(s)\n", (long long)comments.count());

    for (QStringList::Iterator cit = comments.begin();
            cit != comments.end(); ++cit)
      fprintf(stderr, "\t\t\t'%s'\n", (*cit).toLatin1().data());

    fprintf(stderr, "\t\t%lld payload(s)\n", (long long)current->count());

    QListIterator<EQPacketPayload*> pit(*current);
    while (pit.hasNext())
    {
      currentPayload = pit.next();
      if (!currentPayload)
          break;

      seqInfo("\t\t\tdir=%d typename=%s size=%d sizechecktype=%d",
	      currentPayload->dir(), currentPayload->typeName().toLatin1().data(),
	      currentPayload->typeSize(), currentPayload->sizeCheckType());
    }
  }
}

bool EQPacketOPCodeDB::remove(uint16_t opcode)
{
  // remove the opcode object from the opcodes table
  EQPacketOPCode* opcodeObj = m_opcodes.take(opcode);

  if (opcodeObj)
  {
    // remove it from the opcodes by name table
    m_opcodesByName.remove(opcodeObj->name());

    // delete the opcode object
    delete opcodeObj;

    return true;
  }

  return false;
}

bool EQPacketOPCodeDB::remove(const QString& opcodeName)
{
  // remove the opcode object from the opcodes table
  EQPacketOPCode* opcode = m_opcodesByName.take(opcodeName);

  // if found, remove it from the opcodes table
  if (opcode)
  {
      m_opcodes.remove(opcode->opcode());
      delete opcode;

      return true;
  }

  return false;
}

bool EQPacketOPCodeDB::move(uint16_t oldOPCode, uint16_t newOPCode)
{
  // attempt to take an existing opcode object out of the table
  EQPacketOPCode* opcode = m_opcodes.take(oldOPCode);

  // if failed to find an existing opcode object, return failure
  if (!opcode)
    return false;

  // set the new opcode value within the object
  opcode->setOPCode(newOPCode);

  // reinsert the object into the table under the new opcode id
  m_opcodes.insert(newOPCode, opcode);

  return true;
}


bool EQPacketOPCodeDB::move(const QString& oldOPCodeName,
			    const QString& newOPCodeName)
{
  // attempt to take an existing opcode object out of the table
  EQPacketOPCode* opcode = m_opcodesByName.take(oldOPCodeName);

  // if failed to find an existing opcode object, return failure
  if (!opcode)
    return false;

  // set the new opcode value within the object
  opcode->setName(newOPCodeName);

  // reinsert the object into the table under the new opcode id
  m_opcodesByName.insert(newOPCodeName, opcode);

  return true;
}

