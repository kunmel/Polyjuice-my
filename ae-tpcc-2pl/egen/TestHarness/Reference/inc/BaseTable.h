/*
 * Legal Notice
 *
 * This document and associated source code (the "Work") is a preliminary
 * version of a benchmark specification being developed by the TPC. The
 * Work is being made available to the public for review and comment only.
 * The TPC reserves all right, title, and interest to the Work as provided
 * under U.S. and international laws, including without limitation all patent
 * and trademark rights therein.
 *
 * No Warranty
 *
 * 1.1 TO THE MAXIMUM EXTENT PERMITTED BY APPLICABLE LAW, THE INFORMATION
 *     CONTAINED HEREIN IS PROVIDED "AS IS" AND WITH ALL FAULTS, AND THE
 *     AUTHORS AND DEVELOPERS OF THE WORK HEREBY DISCLAIM ALL OTHER
 *     WARRANTIES AND CONDITIONS, EITHER EXPRESS, IMPLIED OR STATUTORY,
 *     INCLUDING, BUT NOT LIMITED TO, ANY (IF ANY) IMPLIED WARRANTIES,
 *     DUTIES OR CONDITIONS OF MERCHANTABILITY, OF FITNESS FOR A PARTICULAR
 *     PURPOSE, OF ACCURACY OR COMPLETENESS OF RESPONSES, OF RESULTS, OF
 *     WORKMANLIKE EFFORT, OF LACK OF VIRUSES, AND OF LACK OF NEGLIGENCE.
 *     ALSO, THERE IS NO WARRANTY OR CONDITION OF TITLE, QUIET ENJOYMENT,
 *     QUIET POSSESSION, CORRESPONDENCE TO DESCRIPTION OR NON-INFRINGEMENT
 *     WITH REGARD TO THE WORK.
 * 1.2 IN NO EVENT WILL ANY AUTHOR OR DEVELOPER OF THE WORK BE LIABLE TO
 *     ANY OTHER PARTY FOR ANY DAMAGES, INCLUDING BUT NOT LIMITED TO THE
 *     COST OF PROCURING SUBSTITUTE GOODS OR SERVICES, LOST PROFITS, LOSS
 *     OF USE, LOSS OF DATA, OR ANY INCIDENTAL, CONSEQUENTIAL, DIRECT,
 *     INDIRECT, OR SPECIAL DAMAGES WHETHER UNDER CONTRACT, TORT, WARRANTY,
 *     OR OTHERWISE, ARISING IN ANY WAY OUT OF THIS OR ANY OTHER AGREEMENT
 *     RELATING TO THE WORK, WHETHER OR NOT SUCH AUTHOR OR DEVELOPER HAD
 *     ADVANCE NOTICE OF THE POSSIBILITY OF SUCH DAMAGES.
 *
 * Contributors
 * - Sergey Vasilevskiy
 */

/*
*   This class represents a general table class interface.
*   It does not contain record type, as that is in TableTemplate.
*/
#ifndef BASE_TABLE_H
#define BASE_TABLE_H

namespace TPCE
{

class CBaseTable
{
protected:
    TIdent          m_iLastRowNumber;   //sequential last row number
    bool            m_bMoreRecords;     //true if more records can be generated, otherwise false
    CRandom         m_rnd;              //random generator - present in all tables
public:
    /*
    *  The Constructor - just initializes member variables.
    *
    *  PARAMETERS:
    *           not applicable.
    *
    *  RETURNS:
    *           not applicable.
    */
    CBaseTable()
        : m_iLastRowNumber(0)
        , m_bMoreRecords(false) //assume
    {
        m_rnd.SetSeed(RNGSeedTableDefault);
    }

    /*
    *  Virtual destructor. Provided so that a sponsor-specific
    *  destructor can be called on destruction from the base-class pointer.
    *
    *  PARAMETERS:
    *           none.
    *
    *  RETURNS:
    *           not applicable.
    */
    virtual ~CBaseTable() {};

    /*
    *  Generate the next record (row).
    *
    *  PARAMETERS:
    *           none.
    *
    *  RETURNS:
    *           true    - if more records are available in the table.
    *           false   - if no more records can be generated.
    */
    virtual bool GenerateNextRecord() = 0;  //abstract class

    /*
    *  Return whether all records in this table have been generated.
    *
    *  PARAMETERS:
    *           none.
    *
    *  RETURNS:
    *           true    - if more records are available in the table.
    *           false   - if no more records can be generated.
    */
    bool MoreRecords() {return m_bMoreRecords;}
};

}   // namespace TPCE

#endif //BASE_TABLE_H
