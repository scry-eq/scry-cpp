/*
 *  spells.h
 *  Copyright 2003 Zaphod (dohpaz@users.sourceforge.net). All Rights Reserved.
 *  Copyright 2001-2005, 2019 by the respective ShowEQ Developers
 *
 *  Contributed to ShowEQ by Zaphod (dohpaz@users.sourceforge.net)
 *  for use under the terms of the GNU General Public License,
 *  incorporated herein by reference.
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
 */

#ifndef SPELLS_H_
#define SPELLS_H_

#ifdef __FreeBSD__
#include <sys/types.h>
#else
#include <cstdint>
#endif
#include <QStringList>
#include <QString>

const size_t playerClasses = 16;

class Spell
{
 public:
  Spell(const QString& spells_enLine);
  ~Spell();

  // accessors
  uint32_t spell() const { return m_spell; }
  const QString& name() const { return m_name; }
  uint8_t level(uint8_t class_) const;
  uint8_t targetType() const { return m_targetType; }
  // Bard (class 8) can cast it: the reliable song discriminator.
  bool isSong() const { return m_classLevels[7] != 255; }
  // "Good effect" column (spellInfo[28] in the post-2018 layout): 1 for
  // beneficial spells (buffs/heals), 0 for detrimental (debuffs/nukes/DoTs).
  // Verified against both the Live (166-field) and EQL (173-field) files.
  bool beneficial() const { return m_beneficial; }
  // Spell icon id (spells_us.txt field 75 on the modern/EQL layout), for the
  // web's spell-icon atlas. 0 = unknown.
  uint16_t icon() const { return m_icon; }

  QString spellField(uint8_t field) const;

  int16_t calcDuration(uint8_t level) const;

 private:
  QString m_name;
  uint32_t m_spell;
  int16_t m_buffDurationFormula;
  int16_t m_buffDurationArgument;
  uint8_t m_targetType;
  bool m_beneficial;
  uint16_t m_icon;
  uint8_t m_classLevels[playerClasses];
};

class Spells
{
 public:
  Spells(const QString& spellsFile);
  ~Spells();
  void loadSpells(const QString& spellsFileName);
  void unloadSpells(void);
    
  const Spell* spell(uint32_t spell) const;
  uint32_t maxSpell() const { return m_maxSpell; }

 private:
  uint32_t m_maxSpell;
  Spell** m_spells;
};

#endif // SPELLS_H_

