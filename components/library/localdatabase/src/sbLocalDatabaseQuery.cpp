/*
//
// BEGIN SONGBIRD GPL
//
// This file is part of the Songbird web player.
//
// Copyright(c) 2005-2008 POTI, Inc.
// http://songbirdnest.com
//
// This file may be licensed under the terms of of the
// GNU General Public License Version 2 (the "GPL").
//
// Software distributed under the License is distributed
// on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either
// express or implied. See the GPL for the specific language
// governing rights and limitations.
//
// You should have received a copy of the GPL along with this
// program. If not, go to http://www.gnu.org/licenses/gpl.html
// or write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
//
// END SONGBIRD GPL
//
*/

#include "sbLocalDatabaseQuery.h"
#include "sbLocalDatabaseSchemaInfo.h"


#include <sbIDatabaseQuery.h>
#include <sbIDatabaseResult.h>
#include <sbILocalDatabasePropertyCache.h>
#include <sbISQLBuilder.h>

#include <nsComponentManagerUtils.h>
#include <sbStandardProperties.h>
#include <sbSQLBuilderCID.h>
#include <sbStringUtils.h>

#define COUNT_COLUMN                NS_LITERAL_STRING("count(1)")
#define CREATED_COLUMN              NS_LITERAL_STRING("created")
#define GUID_COLUMN                 NS_LITERAL_STRING("guid")
#define OBJ_COLUMN                  NS_LITERAL_STRING("obj")
#define OBJSORTABLE_COLUMN          NS_LITERAL_STRING("obj_sortable")
#define OBJSECONDARYSORTABLE_COLUMN NS_LITERAL_STRING("obj_secondary_sortable")
#define MEDIAITEMID_COLUMN          NS_LITERAL_STRING("media_item_id")
#define PROPERTYID_COLUMN           NS_LITERAL_STRING("property_id")
#define ORDINAL_COLUMN              NS_LITERAL_STRING("ordinal")
#define PROPERTYNAME_COLUMN         NS_LITERAL_STRING("property_name")
#define MEDIALISTYPEID_COLUMN       NS_LITERAL_STRING("media_list_type_id")
#define ISLIST_COLUMN               NS_LITERAL_STRING("is_list")
#define ROWID_COLUMN                NS_LITERAL_STRING("rowid")

#define PROPERTIES_TABLE         NS_LITERAL_STRING("resource_properties")
#define PROPERTIES_FTS_TABLE     NS_LITERAL_STRING("resource_properties_fts")
#define PROPERTIES_FTS_ALL_TABLE NS_LITERAL_STRING("resource_properties_fts_all")
#define MEDIAITEMS_TABLE         NS_LITERAL_STRING("media_items")
#define SIMPLEMEDIALISTS_TABLE   NS_LITERAL_STRING("simple_media_lists")
#define PROPERTYIDS_TABLE        NS_LITERAL_STRING("properties")

#define MEDIAITEMS_ALIAS NS_LITERAL_STRING("_mi")
#define CONSTRAINT_ALIAS NS_LITERAL_STRING("_con")
#define GETNOTNULL_ALIAS NS_LITERAL_STRING("_getnotnull")
#define GETNULL_ALIAS    NS_LITERAL_STRING("_getnull")
#define SORT_ALIAS       NS_LITERAL_STRING("_sort")
#define DISTINCT_ALIAS   NS_LITERAL_STRING("_d")
#define CONPROP_ALIAS    NS_LITERAL_STRING("_conprop")

#define ORDINAL_PROPERTY NS_LITERAL_STRING(SB_PROPERTY_ORDINAL)
#define ISLIST_PROPERTY  NS_LITERAL_STRING(SB_PROPERTY_ISLIST)

sbLocalDatabaseQuery::sbLocalDatabaseQuery(const nsAString& aBaseTable,
                                           const nsAString& aBaseConstraintColumn,
                                           PRUint32 aBaseConstraintValue,
                                           const nsAString& aBaseForeignKeyColumn,
                                           nsTArray<sbLocalDatabaseGUIDArray::FilterSpec>* aFilters,
                                           nsTArray<sbLocalDatabaseGUIDArray::SortSpec>* aSorts,
                                           PRBool aIsDistinct,
                                           PRBool aDistinctWithSortableValues,
                                           sbILocalDatabasePropertyCache* aPropertyCache) :
  mBaseTable(aBaseTable),
  mBaseConstraintColumn(aBaseConstraintColumn),
  mBaseConstraintValue(aBaseConstraintValue),
  mBaseForeignKeyColumn(aBaseForeignKeyColumn),
  mFilters(aFilters),
  mSorts(aSorts),
  mIsDistinct(aIsDistinct),
  mDistinctWithSortableValues(aDistinctWithSortableValues),
  mPropertyCache(aPropertyCache),
  mHasSearch(PR_FALSE)
{
  mIsFullLibrary = mBaseTable.Equals(MEDIAITEMS_TABLE);

  mBuilder = do_CreateInstance(SB_SQLBUILDER_SELECT_CONTRACTID);
  NS_ASSERTION(mBuilder, "Could not create builder");

  // Check to see if we have a search.  This determines if we need to ask for
  // distinct results or not
  // Combine all searches with the same term into one search
  PRUint32 len = mFilters->Length();
  for (PRUint32 i = 0; i < len; i++) {
    if (mFilters->ElementAt(i).isSearch) {
      mHasSearch = PR_TRUE;
      break;
    }
  }

}

nsresult
sbLocalDatabaseQuery::GetFullCountQuery(nsAString& aQuery)
{
  nsresult rv;

  rv = mBuilder->Reset();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = AddCountColumns();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = AddBaseTable();
  NS_ENSURE_SUCCESS(rv, rv);

  if (mIsDistinct) {
    rv = AddDistinctConstraint();
    NS_ENSURE_SUCCESS(rv, rv);
  }

  rv = AddFilters();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mBuilder->ToString(aQuery);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult
sbLocalDatabaseQuery::GetFullGuidRangeQuery(nsAString& aQuery)
{
  nsresult rv;

  rv = mBuilder->Reset();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = AddGuidColumns(PR_FALSE);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = AddBaseTable();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = AddFilters();
  NS_ENSURE_SUCCESS(rv, rv);

  if (mIsDistinct) {
    rv = AddDistinctGroupBy();
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // If we are sorted on a top level property, we need to add a constraint
  // to ensure we only get rows with non-null values.  Note that we don't have
  // to do this when it is not a top level property because we don't get nulls
  // by virtue of the join
  if (SB_IsTopLevelProperty(mSorts->ElementAt(0).property)) {
    nsAutoString columnName;
    rv = SB_GetTopLevelPropertyColumn(mSorts->ElementAt(0).property, columnName);
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<sbISQLBuilderCriterion> criterion;
    rv = mBuilder->CreateMatchCriterionNull(MEDIAITEMS_ALIAS,
                                            columnName,
                                            sbISQLSelectBuilder::MATCH_NOTEQUALS,
                                            getter_AddRefs(criterion));
    rv = mBuilder->AddCriterion(criterion);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  rv = AddPrimarySort();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = AddRange();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mBuilder->ToString(aQuery);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult
sbLocalDatabaseQuery::GetNonNullCountQuery(nsAString& aQuery)
{
  nsresult rv;

  rv = mBuilder->Reset();
  NS_ENSURE_SUCCESS(rv, rv);

  // There are no non-null rows in a distinct count query
  if (mIsDistinct) {
    aQuery = EmptyString();
    return NS_OK;
  }

  rv = AddCountColumns();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = AddBaseTable();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = AddFilters();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = AddNonNullPrimarySortConstraint();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mBuilder->ToString(aQuery);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult
sbLocalDatabaseQuery::GetNullGuidRangeQuery(nsAString& aQuery)
{
  nsresult rv;

  rv = mBuilder->Reset();
  NS_ENSURE_SUCCESS(rv, rv);

  // There are no non-null rows in a distinct count query
  if (mIsDistinct) {
    aQuery = EmptyString();
    return NS_OK;
  }

  rv = AddGuidColumns(PR_TRUE);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = AddBaseTable();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = AddFilters();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = AddJoinToGetNulls();
  NS_ENSURE_SUCCESS(rv, rv);

  // Always sort nulls by ascending media item so their positions never change
  rv = mBuilder->AddOrder(MEDIAITEMS_ALIAS,
                          MEDIAITEMID_COLUMN,
                          PR_TRUE);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = AddRange();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mBuilder->ToString(aQuery);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult
sbLocalDatabaseQuery::GetPrefixSearchQuery(nsAString& aQuery)
{
  nsresult rv;

  rv = mBuilder->Reset();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = AddCountColumns();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = AddBaseTable();
  NS_ENSURE_SUCCESS(rv, rv);

  if (mIsDistinct) {
    rv = AddDistinctConstraint();
    NS_ENSURE_SUCCESS(rv, rv);
  }

  rv = AddFilters();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = AddPrimarySort();
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<sbISQLBuilderCriterion> criterion;
  if (mSorts->ElementAt(0).property.Equals(ORDINAL_PROPERTY)) {

    nsAutoString baseTable;
    rv = mBuilder->GetBaseTableName(baseTable);
    NS_ENSURE_SUCCESS(rv, rv);

    if (baseTable.Equals(SIMPLEMEDIALISTS_TABLE)) {
      rv = mBuilder->CreateMatchCriterionParameter(CONSTRAINT_ALIAS,
                                                   ORDINAL_COLUMN,
                                                   sbISQLSelectBuilder::MATCH_LESS,
                                                   getter_AddRefs(criterion));
      NS_ENSURE_SUCCESS(rv, rv);
    }
    else {
      return NS_OK;
    }
  }
  else {
    rv = mBuilder->CreateMatchCriterionParameter(SORT_ALIAS,
                                                 OBJSORTABLE_COLUMN,
                                                 sbISQLSelectBuilder::MATCH_LESS,
                                                 getter_AddRefs(criterion));
    NS_ENSURE_SUCCESS(rv, rv);
  }
  rv = mBuilder->AddCriterion(criterion);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mBuilder->ToString(aQuery);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult
sbLocalDatabaseQuery::GetResortQuery(nsAString& aQuery)
{
  NS_ENSURE_FALSE(mIsDistinct, NS_ERROR_UNEXPECTED);
  NS_ENSURE_TRUE(mSorts->Length() > 1, NS_ERROR_UNEXPECTED);

  nsresult rv;

  rv = mBuilder->Reset();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = AddResortColumns();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = AddBaseTable();
  NS_ENSURE_SUCCESS(rv, rv);

  // Constrain the rows in the base table to a given value parameter
  nsCOMPtr<sbISQLBuilderCriterion> criterion;
  if (SB_IsTopLevelProperty(mSorts->ElementAt(0).property)) {

    // If the primary sort is a top level property, we can put the value
    // constraint directly on the media items table
    nsString columnName;
    rv = SB_GetTopLevelPropertyColumn(mSorts->ElementAt(0).property,
                                      columnName);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = mBuilder->CreateMatchCriterionParameter(MEDIAITEMS_ALIAS,
                                                 columnName,
                                                 sbISQLSelectBuilder::MATCH_EQUALS,
                                                 getter_AddRefs(criterion));
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mBuilder->AddCriterion(criterion);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  else {
    // Join the properties table to apply the constraint
    rv = mBuilder->AddJoin(sbISQLSelectBuilder::JOIN_INNER,
                           PROPERTIES_TABLE,
                           CONPROP_ALIAS,
                           MEDIAITEMID_COLUMN,
                           MEDIAITEMS_ALIAS,
                           MEDIAITEMID_COLUMN);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = mBuilder->CreateMatchCriterionLong(CONPROP_ALIAS,
                                            PROPERTYID_COLUMN,
                                            sbISQLSelectBuilder::MATCH_EQUALS,
                                            GetPropertyId(mSorts->ElementAt(0).property),
                                            getter_AddRefs(criterion));
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mBuilder->AddCriterion(criterion);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = mBuilder->CreateMatchCriterionParameter(CONPROP_ALIAS,
                                                 OBJSORTABLE_COLUMN,
                                                 sbISQLSelectBuilder::MATCH_EQUALS,
                                                 getter_AddRefs(criterion));
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mBuilder->AddCriterion(criterion);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  rv = AddFilters();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = AddMultiSorts();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mBuilder->ToString(aQuery);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult
sbLocalDatabaseQuery::GetNullResortQuery(nsAString& aQuery)
{
  NS_ENSURE_FALSE(mIsDistinct, NS_ERROR_UNEXPECTED);
  NS_ENSURE_TRUE(mSorts->Length() > 1, NS_ERROR_UNEXPECTED);

  if (SB_IsTopLevelProperty(mSorts->ElementAt(0).property)) {
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  nsresult rv;

  rv = mBuilder->Reset();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = AddResortColumns();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = AddBaseTable();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = AddFilters();
  NS_ENSURE_SUCCESS(rv, rv);

  // Left join the properties table to the base table includig a null
  // constraint on the obj_sortable column
  nsCOMPtr<sbISQLBuilderCriterion> criterionGuid;
  rv = mBuilder->CreateMatchCriterionTable(NS_LITERAL_STRING("_pX"),
                                           MEDIAITEMID_COLUMN,
                                           sbISQLSelectBuilder::MATCH_EQUALS,
                                           MEDIAITEMS_ALIAS,
                                           MEDIAITEMID_COLUMN,
                                           getter_AddRefs(criterionGuid));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<sbISQLBuilderCriterion> criterionProperty;
  rv = mBuilder->CreateMatchCriterionLong(NS_LITERAL_STRING("_pX"),
                                          NS_LITERAL_STRING("property_id"),
                                          sbISQLSelectBuilder::MATCH_EQUALS,
                                          GetPropertyId(mSorts->ElementAt(0).property),
                                          getter_AddRefs(criterionProperty));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<sbISQLBuilderCriterion> criterion;
  rv = mBuilder->CreateAndCriterion(criterionGuid,
                                    criterionProperty,
                                    getter_AddRefs(criterion));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mBuilder->AddJoinWithCriterion(sbISQLSelectBuilder::JOIN_LEFT,
                                      PROPERTIES_TABLE,
                                      NS_LITERAL_STRING("_pX"),
                                      criterion);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mBuilder->CreateMatchCriterionNull(NS_LITERAL_STRING("_pX"),
                                          OBJSORTABLE_COLUMN,
                                          sbISQLSelectBuilder::MATCH_EQUALS,
                                          getter_AddRefs(criterion));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mBuilder->AddCriterion(criterion);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = AddMultiSorts();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mBuilder->ToString(aQuery);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

PRBool
sbLocalDatabaseQuery::GetIsFullLibrary()
{
  return mIsFullLibrary;
}

nsresult
sbLocalDatabaseQuery::AddCountColumns()
{
  nsresult rv;

  if (mIsDistinct) {
    if (SB_IsTopLevelProperty(mSorts->ElementAt(0).property)) {
        nsAutoString columnName;
        rv = SB_GetTopLevelPropertyColumn(mSorts->ElementAt(0).property,
                                          columnName);
        NS_ENSURE_SUCCESS(rv, rv);

        nsAutoString count;
        count.AssignLiteral("count(distinct _mi.");
        count.Append(columnName);
        count.AppendLiteral(")");

        rv = mBuilder->AddColumn(EmptyString(), count);
        NS_ENSURE_SUCCESS(rv, rv);
    }
    else {
      rv = mBuilder->AddColumn(EmptyString(),
                               NS_LITERAL_STRING("count(distinct _d.obj_sortable)"));
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }
  else {
    rv = mBuilder->AddColumn(EmptyString(), COUNT_COLUMN);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

nsresult
sbLocalDatabaseQuery::AddGuidColumns(PRBool aIsNull)
{
  nsresult rv;

  rv = mBuilder->AddColumn(MEDIAITEMS_ALIAS, MEDIAITEMID_COLUMN);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mBuilder->AddColumn(MEDIAITEMS_ALIAS, GUID_COLUMN);
  NS_ENSURE_SUCCESS(rv, rv);

  if (aIsNull) {
    rv = mBuilder->AddColumn(EmptyString(), NS_LITERAL_STRING("''"));
    NS_ENSURE_SUCCESS(rv, rv);
  }
  else {
    if (SB_IsTopLevelProperty(mSorts->ElementAt(0).property)) {
      nsAutoString columnName;
      rv = SB_GetTopLevelPropertyColumn(mSorts->ElementAt(0).property, columnName);
      NS_ENSURE_SUCCESS(rv, rv);

      rv = mBuilder->AddColumn(MEDIAITEMS_ALIAS, columnName);
      NS_ENSURE_SUCCESS(rv, rv);
    }
    else {
      if (mSorts->ElementAt(0).property.Equals(ORDINAL_PROPERTY)) {

        if (mBaseTable.Equals(SIMPLEMEDIALISTS_TABLE)) {
          rv = mBuilder->AddColumn(CONSTRAINT_ALIAS,
                                   ORDINAL_COLUMN);
          NS_ENSURE_SUCCESS(rv, rv);
        }
      }
      else {
        if (mIsDistinct && !mDistinctWithSortableValues) {
          rv = mBuilder->AddColumn(SORT_ALIAS, OBJ_COLUMN);
          NS_ENSURE_SUCCESS(rv, rv);
        }
        else {
          rv = mBuilder->AddColumn(SORT_ALIAS, OBJSORTABLE_COLUMN);
          NS_ENSURE_SUCCESS(rv, rv);
        }
      }
    }
  }

  // If this query is on the simple media list table, add the ordinal
  // as the next column
  if (mBaseTable.Equals(SIMPLEMEDIALISTS_TABLE)) {
    rv = mBuilder->AddColumn(CONSTRAINT_ALIAS,
                             ORDINAL_COLUMN);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  else {
    rv = mBuilder->AddColumn(EmptyString(), NS_LITERAL_STRING("''"));
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // If this is a library query, add the rowid from the media_items table,
  // otherwise add it from the simple_media_lists table
  nsString base;
  if (mIsFullLibrary) {
    base = MEDIAITEMS_ALIAS;
  }
  else {
    base = CONSTRAINT_ALIAS;
  }

  rv = mBuilder->AddColumn(base, ROWID_COLUMN);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult
sbLocalDatabaseQuery::AddBaseTable()
{
  nsresult rv;

  if (mIsFullLibrary) {
    rv = mBuilder->SetBaseTableName(MEDIAITEMS_TABLE);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = mBuilder->SetBaseTableAlias(MEDIAITEMS_ALIAS);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  else {
    /*
     * Join the base table to the media items table and add the needed
     * constraint.
     */
    rv = mBuilder->SetBaseTableName(mBaseTable);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = mBuilder->SetBaseTableAlias(CONSTRAINT_ALIAS);
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<sbISQLBuilderCriterion> criterion;
    rv = mBuilder->CreateMatchCriterionLong(CONSTRAINT_ALIAS,
                                            mBaseConstraintColumn,
                                            sbISQLSelectBuilder::MATCH_EQUALS,
                                            mBaseConstraintValue,
                                            getter_AddRefs(criterion));
    NS_ENSURE_SUCCESS(rv, rv);

    rv = mBuilder->AddCriterion(criterion);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = mBuilder->AddJoin(sbISQLSelectBuilder::JOIN_INNER,
                           MEDIAITEMS_TABLE,
                           MEDIAITEMS_ALIAS,
                           MEDIAITEMID_COLUMN,
                           CONSTRAINT_ALIAS,
                           mBaseForeignKeyColumn);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

nsresult
sbLocalDatabaseQuery::AddFilters()
{
  nsresult rv;

  PRUint32 joinNum = 0;

  // Add the filters as joins
  PRUint32 len = mFilters->Length();
  for (PRUint32 i = 0; i < len; i++) {
    const sbLocalDatabaseGUIDArray::FilterSpec& fs = mFilters->ElementAt(i);

    if (!fs.isSearch) {

      PRBool isTopLevelProperty = SB_IsTopLevelProperty(fs.property);
      nsCOMPtr<sbISQLBuilderCriterion> criterion;

      // If the property is the "is list" property, we handle this in a
      // special way.  If the value is 1, we filter the media_list_type_id
      // column on not null, if the value is 0 we filter said column on null
      if (fs.property.Equals(ISLIST_PROPERTY)) {
        PRUint32 numValues = fs.values.Length();
        NS_ENSURE_STATE(numValues);
        nsCOMPtr<sbISQLBuilderCriterion> nullCriterion;
        PRUint32 matchType;
        if (fs.values[0].EqualsLiteral("0")) {
          matchType = sbISQLSelectBuilder::MATCH_EQUALS;
        }
        else {
          matchType = sbISQLSelectBuilder::MATCH_NOTEQUALS;
        }
        
        // XXX HACK XXX HACK XXX HACK UGH
        // The |media_list_type_id is null| constraint will cause sqlite to
        // access the data using the idx_media_items_media_list_type_id index
        // which turns out to be a poor choice because the values in that
        // column are mostly null.  Using |+_mi.media_list_type_id| in the
        // criterion causes sqlite to not use the index, and should allow it
        // to select a better one.
        //
        // Possibly related sqlite bug:
        //   http://www.sqlite.org/cvstrac/tktview?tn=3036
        // I also posted about this issue on sqlite-users:
        //   http://www.mail-archive.com/sqlite-users@sqlite.org/msg32924.html
        nsString indexKillingAlias;
        indexKillingAlias.AssignLiteral("+");
        indexKillingAlias.Append(MEDIAITEMS_ALIAS);

        rv = mBuilder->CreateMatchCriterionNull(indexKillingAlias,
                                                MEDIALISTYPEID_COLUMN,
                                                matchType,
                                                getter_AddRefs(nullCriterion));
        NS_ENSURE_SUCCESS(rv, rv);

        rv = mBuilder->AddCriterion(nullCriterion);
        NS_ENSURE_SUCCESS(rv, rv);
      }
      else {
        nsCOMPtr<sbISQLBuilderCriterionIn> inCriterion;
        if (isTopLevelProperty) {

          // Add the constraint for the top level table.
          nsAutoString columnName;
          rv = SB_GetTopLevelPropertyColumn(fs.property, columnName);
          NS_ENSURE_SUCCESS(rv, rv);

          rv = mBuilder->CreateMatchCriterionIn(MEDIAITEMS_ALIAS,
                                                columnName,
                                                getter_AddRefs(inCriterion));
          NS_ENSURE_SUCCESS(rv, rv);
        }
        else {
          nsAutoString tableAlias;
          tableAlias.AppendLiteral("_p");
          tableAlias.AppendInt(joinNum++);

          rv = mBuilder->AddJoin(sbISQLSelectBuilder::JOIN_INNER,
                                 PROPERTIES_TABLE,
                                 tableAlias,
                                 MEDIAITEMID_COLUMN,
                                 MEDIAITEMS_ALIAS,
                                 MEDIAITEMID_COLUMN);
          NS_ENSURE_SUCCESS(rv, rv);

          // Add the propery constraint for the filter
          rv = mBuilder->CreateMatchCriterionLong(tableAlias,
                                                  NS_LITERAL_STRING("property_id"),
                                                  sbISQLSelectBuilder::MATCH_EQUALS,
                                                  GetPropertyId(fs.property),
                                                  getter_AddRefs(criterion));
          NS_ENSURE_SUCCESS(rv, rv);

          rv = mBuilder->AddCriterion(criterion);
          NS_ENSURE_SUCCESS(rv, rv);

          rv = mBuilder->CreateMatchCriterionIn(tableAlias,
                                                OBJSORTABLE_COLUMN,
                                                getter_AddRefs(inCriterion));
          NS_ENSURE_SUCCESS(rv, rv);
        }

        // For each filter value, add the term to an IN constraint
        PRUint32 numValues = fs.values.Length();
        for (PRUint32 j = 0; j < numValues; j++) {
          const nsAString& filterTerm = fs.values[j];
          rv = inCriterion->AddString(filterTerm);
          NS_ENSURE_SUCCESS(rv, rv);
        }

        rv = mBuilder->AddCriterion(inCriterion);
        NS_ENSURE_SUCCESS(rv, rv);
      }
    }
  }

  // Add search constraints

  PRInt32 searchIndex = -1;
  PRBool isEverythingSearch = PR_FALSE;

  for (PRUint32 i = 0; i < len; i++) {
    const sbLocalDatabaseGUIDArray::FilterSpec& fs = mFilters->ElementAt(i);
    if (fs.isSearch) {
      if (searchIndex < 0) {

        if (SB_IsTopLevelProperty(fs.property)) {
          NS_WARNING("Top level properties not supported in search");
          return NS_ERROR_INVALID_ARG;
        }

        searchIndex = i;
        if (fs.property.EqualsLiteral("*")) {
          isEverythingSearch = PR_TRUE;
        }
        break;
      }
    }
  }

  if (searchIndex >= 0) {

    // XXX Now that we are using sqlite FTS, we need to live with its
    // limitations, such as not being able to have more than one MATCH
    // in there WHERE clause.  This prevents us from joining the fts table
    // more than once.  From my testing, it seems like the fastest way to
    // do this is to use a single fts join with both an obj MATCH and
    // propertyid IN (when needed) in the where clause.  The match string
    // will be each string in the value list for the filter.  The propertyid
    // in list will be the ids of all of the properties from all of the
    // filters (if needed).  Note that this means that every search filter
    // must have the same value list.  This is enforced in
    // sbLocalDatabaseMediaListView::SetSearchConstraint

    // XXXAus: Because of bug 9488, all searches must use the everythingSearch.
    // See bug 9488 and bug 9617 for more information.
    isEverythingSearch = PR_TRUE;

    if (isEverythingSearch) {
      // Join the all fts table.  The foreign key of this table is the
      // media item id so we can simply join it to _mi
      rv = mBuilder->AddJoin(sbISQLSelectBuilder::JOIN_INNER,
                             PROPERTIES_FTS_ALL_TABLE,
                             NS_LITERAL_STRING("_fts"),
                             ROWID_COLUMN,
                             MEDIAITEMS_ALIAS,
                             MEDIAITEMID_COLUMN);
      NS_ENSURE_SUCCESS(rv, rv);
    }

    /* XXXAus: resource_properties_fts is disabled. See bug 9488 and bug 9617.
    else {
      // This is not the everything search so we need to use the per-property
      // fts table.  The foreign key on that table is the resource_properties
      // row id, so we must join both a "bridge" table as well as the fts table
      rv = mBuilder->AddJoin(sbISQLSelectBuilder::JOIN_INNER,
                             PROPERTIES_TABLE,
                             NS_LITERAL_STRING("_fts_bridge"),
                             MEDIAITEMID_COLUMN,
                             MEDIAITEMS_ALIAS,
                             MEDIAITEMID_COLUMN);
      NS_ENSURE_SUCCESS(rv, rv);

      rv = mBuilder->AddJoin(sbISQLSelectBuilder::JOIN_INNER,
                             PROPERTIES_FTS_TABLE,
                             NS_LITERAL_STRING("_fts"),
                             ROWID_COLUMN,
                             NS_LITERAL_STRING("_fts_bridge"),
                             ROWID_COLUMN);
      NS_ENSURE_SUCCESS(rv, rv);
    }
    */

    // Add the match constraint
    nsString match;
    const sbLocalDatabaseGUIDArray::FilterSpec& fs =
      mFilters->ElementAt(searchIndex);

    for (PRUint32 i = 0; i < fs.values.Length(); i++) {

      nsString stripped(fs.values[i]);

      // '*' and '-' are special FTS operators. Make sure they are not in the
      // search filter otherwise the query will fail with a logic error.
      PRInt32 pos = 0;

      do {
        pos = nsString_FindCharInSet(stripped, "*-", pos);
        if(pos != -1) {
          if(static_cast<PRUint32>(pos) + 1 < stripped.Length() &&
             stripped[pos + 1] != '*' && 
             stripped[pos + 1] != '-' &&
             stripped[pos + 1] != ' ') {
            stripped.Replace(pos, 1, NS_LITERAL_STRING(" NEAR/0 "));
            pos += 8;
          }
          else {
            stripped.Cut(pos, 1);
          }
        }
      }
      while(pos != -1);

      // Quotes have to be escaped, otherwise if they are unbalanced they will
      // cause a SQL error when the statement is compiled :( We can't actually
      // use parameter binding here because of the way the underlying GUID array
      // that uses this object is built. The GUID array completely relies on the
      // automatic query building to generate the queries it uses. Because of this
      // it's completely unaware of which query it's running, hence we can't use
      // parameter binding.
      nsString_ReplaceSubstring(stripped, 
                                NS_LITERAL_STRING("\""), 
                                NS_LITERAL_STRING("\\\""));
      nsString_ReplaceSubstring(stripped, 
                                NS_LITERAL_STRING("'"), 
                                NS_LITERAL_STRING("\\'"));

      match.AppendLiteral("'");
      match.Append(stripped);
      match.AppendLiteral("*'");

      if (i + 1 < fs.values.Length()) {
        match.AppendLiteral(" ");
      }
    }

    nsCOMPtr<sbISQLBuilderCriterion> criterion;
    rv = mBuilder->CreateMatchCriterionString(NS_LITERAL_STRING("_fts"),
                                              isEverythingSearch ?
                                                NS_LITERAL_STRING("alldata") :
                                                NS_LITERAL_STRING("obj_searchable"),
                                              sbISQLSelectBuilder::MATCH_MATCH,
                                              match,
                                              getter_AddRefs(criterion));
    NS_ENSURE_SUCCESS(rv, rv);

    rv = mBuilder->AddCriterion(criterion);
    NS_ENSURE_SUCCESS(rv, rv);

    // Add the property ID constraint if needed
    if (!isEverythingSearch) {
      nsCOMPtr<sbISQLBuilderCriterion> criterion;
      rv = mBuilder->CreateMatchCriterionLong(NS_LITERAL_STRING("_fts"),
                                              NS_LITERAL_STRING("propertyid"),
                                              sbISQLSelectBuilder::MATCH_EQUALS,
                                              GetPropertyId(fs.property),
                                              getter_AddRefs(criterion));
      NS_ENSURE_SUCCESS(rv, rv);

      rv = mBuilder->AddCriterion(criterion);
      NS_ENSURE_SUCCESS(rv, rv);
    }

  }

  // If this is a top level distinct query, make sure the primary sort property
  // is never null
  if (mIsDistinct && SB_IsTopLevelProperty(mSorts->ElementAt(0).property)) {
    nsCOMPtr<sbISQLBuilderCriterion> criterion;
    nsAutoString columnName;
    rv = SB_GetTopLevelPropertyColumn(mSorts->ElementAt(0).property, columnName);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = mBuilder->CreateMatchCriterionNull(MEDIAITEMS_ALIAS,
                                            columnName,
                                            sbISQLSelectBuilder::MATCH_NOTEQUALS,
                                            getter_AddRefs(criterion));
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mBuilder->AddCriterion(criterion);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

nsresult
sbLocalDatabaseQuery::AddRange()
{
  nsresult rv;

  rv = mBuilder->SetOffsetIsParameter(PR_TRUE);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mBuilder->SetLimitIsParameter(PR_TRUE);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult
sbLocalDatabaseQuery::AddPrimarySort()
{
  nsresult rv;

  /*
   * If this is a top level propery, simply add the sort
   */
  if (SB_IsTopLevelProperty(mSorts->ElementAt(0).property)) {
    nsAutoString columnName;
    rv = SB_GetTopLevelPropertyColumn(mSorts->ElementAt(0).property, columnName);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = mBuilder->AddOrder(MEDIAITEMS_ALIAS,
                            columnName,
                            mSorts->ElementAt(0).ascending);
    NS_ENSURE_SUCCESS(rv, rv);

    /*
     * Add the media_item_id to the sort so like values always sort the same
     */
    rv = mBuilder->AddOrder(MEDIAITEMS_ALIAS,
                            MEDIAITEMID_COLUMN,
                            mSorts->ElementAt(0).ascending);
    NS_ENSURE_SUCCESS(rv, rv);

    return NS_OK;
  }

  /*
   * If this is the custom sort, sort by the "ordinal" column in the
   * "simple_media_lists" table.  Make sure that base table is actually
   * the simple media lists table before proceeding.
   */
  if (mSorts->ElementAt(0).property.Equals(ORDINAL_PROPERTY)) {
    nsAutoString baseTable;
    rv = mBuilder->GetBaseTableName(baseTable);
    NS_ENSURE_SUCCESS(rv, rv);

    if (baseTable.Equals(SIMPLEMEDIALISTS_TABLE)) {
      rv = mBuilder->AddOrder(CONSTRAINT_ALIAS,
                              ORDINAL_COLUMN,
                              mSorts->ElementAt(0).ascending);
      NS_ENSURE_SUCCESS(rv, rv);

      return NS_OK;
    }

    // Default to the created property for non-lists. This addresses
    // bug 13287 where the sort wasn't specified for some lists
    mBuilder->AddOrder(CONSTRAINT_ALIAS,
                       CREATED_COLUMN,
                       mSorts->ElementAt(0).ascending);
    NS_ENSURE_SUCCESS(rv, rv);
    return NS_OK;
  }

  /*
   * Join an instance of the properties table to the base table
   */
  rv = mBuilder->AddJoin(sbISQLSelectBuilder::JOIN_INNER,
                         PROPERTIES_TABLE,
                         SORT_ALIAS,
                         MEDIAITEMID_COLUMN,
                         MEDIAITEMS_ALIAS,
                         MEDIAITEMID_COLUMN);
  NS_ENSURE_SUCCESS(rv, rv);

  /*
   * Restrict the sort table to the sort property
   */
  nsCOMPtr<sbISQLBuilderCriterion> criterion;
  rv = mBuilder->CreateMatchCriterionLong(SORT_ALIAS,
                                          PROPERTYID_COLUMN,
                                          sbISQLSelectBuilder::MATCH_EQUALS,
                                          GetPropertyId(mSorts->ElementAt(0).property),
                                          getter_AddRefs(criterion));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mBuilder->AddCriterion(criterion);
  NS_ENSURE_SUCCESS(rv, rv);

  /*
   * Add a sort on the primary sort
   */
  rv = mBuilder->AddOrder(SORT_ALIAS,
                          OBJSORTABLE_COLUMN,
                          mSorts->ElementAt(0).ascending);
  NS_ENSURE_SUCCESS(rv, rv);

  /*
   * Add a sort on the pre-baked secondary sort, unless
   * this is a distinct list, in which case the secondary
   * sort will make no difference.
   */
  if (!mIsDistinct) {
    
    rv = mBuilder->AddOrder(SORT_ALIAS,
                            OBJSECONDARYSORTABLE_COLUMN,
                            mSorts->ElementAt(0).ascending);
    NS_ENSURE_SUCCESS(rv, rv);
    
    /*
     * Sort on media_item_id to make the order of the rows always the same.  Make
     * sure we sort on the same direction as the primary sort so reversing the
     * primary sort will reverse the ordering when the primary sort values are
     * the same.
     */
    rv = mBuilder->AddOrder(SORT_ALIAS,
                            MEDIAITEMID_COLUMN,
                            mSorts->ElementAt(0).ascending);
    NS_ENSURE_SUCCESS(rv, rv);
  }



  return NS_OK;

}

nsresult
sbLocalDatabaseQuery::AddNonNullPrimarySortConstraint()
{
  nsresult rv;

  nsCOMPtr<sbISQLBuilderCriterion> criterion;

  if (SB_IsTopLevelProperty(mSorts->ElementAt(0).property)) {
    nsAutoString columnName;
    rv = SB_GetTopLevelPropertyColumn(mSorts->ElementAt(0).property, columnName);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = mBuilder->CreateMatchCriterionNull(MEDIAITEMS_ALIAS,
                                            columnName,
                                            sbISQLSelectBuilder::MATCH_NOTEQUALS,
                                            getter_AddRefs(criterion));
    rv = mBuilder->AddCriterion(criterion);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  else {
    if (mSorts->ElementAt(0).property.Equals(ORDINAL_PROPERTY)) {

      nsAutoString baseTable;
      rv = mBuilder->GetBaseTableName(baseTable);
      NS_ENSURE_SUCCESS(rv, rv);

      if (baseTable.Equals(SIMPLEMEDIALISTS_TABLE)) {
        rv = mBuilder->CreateMatchCriterionNull(CONSTRAINT_ALIAS,
                                                ORDINAL_COLUMN,
                                                sbISQLSelectBuilder::MATCH_NOTEQUALS,
                                                getter_AddRefs(criterion));
        rv = mBuilder->AddCriterion(criterion);
        NS_ENSURE_SUCCESS(rv, rv);
      }
    }
    else {
      rv = mBuilder->AddJoin(sbISQLSelectBuilder::JOIN_INNER,
                             PROPERTIES_TABLE,
                             GETNOTNULL_ALIAS,
                             MEDIAITEMID_COLUMN,
                             MEDIAITEMS_ALIAS,
                             MEDIAITEMID_COLUMN);
      NS_ENSURE_SUCCESS(rv, rv);

      rv = mBuilder->CreateMatchCriterionLong(GETNOTNULL_ALIAS,
                                              PROPERTYID_COLUMN,
                                              sbISQLSelectBuilder::MATCH_EQUALS,
                                              GetPropertyId(mSorts->ElementAt(0).property),
                                              getter_AddRefs(criterion));
      NS_ENSURE_SUCCESS(rv, rv);
      rv = mBuilder->AddCriterion(criterion);
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }

  return NS_OK;
}

nsresult
sbLocalDatabaseQuery::AddDistinctConstraint()
{
  nsresult rv;

  // When doing a distinct query, add a constraint so we only select media items
  // that have a value for the primary sort
  if (SB_IsTopLevelProperty(mSorts->ElementAt(0).property)) {
    nsAutoString columnName;
    rv = SB_GetTopLevelPropertyColumn(mSorts->ElementAt(0).property, columnName);
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<sbISQLBuilderCriterion> notNull;
    rv = mBuilder->CreateMatchCriterionNull(MEDIAITEMS_ALIAS,
                                            columnName,
                                            sbISQLSelectBuilder::MATCH_NOTEQUALS,
                                            getter_AddRefs(notNull));
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<sbISQLBuilderCriterion> notEmptyString;
    rv = mBuilder->CreateMatchCriterionString(MEDIAITEMS_ALIAS,
                                              columnName,
                                              sbISQLSelectBuilder::MATCH_NOTEQUALS,
                                              EmptyString(),
                                              getter_AddRefs(notEmptyString));
    NS_ENSURE_SUCCESS(rv, rv);


    nsCOMPtr<sbISQLBuilderCriterion> criterion;
    rv = mBuilder->CreateAndCriterion(notNull,
                                      notEmptyString,
                                      getter_AddRefs(criterion));
    NS_ENSURE_SUCCESS(rv, rv);

    rv = mBuilder->AddCriterion(criterion);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  else {
    rv = mBuilder->AddJoin(sbISQLSelectBuilder::JOIN_INNER,
                           PROPERTIES_TABLE,
                           DISTINCT_ALIAS,
                           MEDIAITEMID_COLUMN,
                           MEDIAITEMS_ALIAS,
                           MEDIAITEMID_COLUMN);
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<sbISQLBuilderCriterion> property;
    rv = mBuilder->CreateMatchCriterionLong(DISTINCT_ALIAS,
                                            PROPERTYID_COLUMN,
                                            sbISQLSelectBuilder::MATCH_EQUALS,
                                            GetPropertyId(mSorts->ElementAt(0).property),
                                            getter_AddRefs(property));
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<sbISQLBuilderCriterion> notEmptyString;
    rv = mBuilder->CreateMatchCriterionString(DISTINCT_ALIAS,
                                              OBJSORTABLE_COLUMN,
                                              sbISQLSelectBuilder::MATCH_NOTEQUALS,
                                              EmptyString(),
                                              getter_AddRefs(notEmptyString));
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<sbISQLBuilderCriterion> criterion;
    rv = mBuilder->CreateAndCriterion(property,
                                      notEmptyString,
                                      getter_AddRefs(criterion));
    NS_ENSURE_SUCCESS(rv, rv);

    rv = mBuilder->AddCriterion(criterion);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

nsresult
sbLocalDatabaseQuery::AddDistinctGroupBy()
{
  nsresult rv;

  if (SB_IsTopLevelProperty(mSorts->ElementAt(0).property)) {
    nsAutoString columnName;
    rv = SB_GetTopLevelPropertyColumn(mSorts->ElementAt(0).property, columnName);
    NS_ENSURE_SUCCESS(rv, rv);

    // Prevent empty values
    nsCOMPtr<sbISQLBuilderCriterion> notEmptyString;
    rv = mBuilder->CreateMatchCriterionString(MEDIAITEMS_ALIAS,
                                              columnName,
                                              sbISQLSelectBuilder::MATCH_NOTEQUALS,
                                              EmptyString(),
                                              getter_AddRefs(notEmptyString));
    NS_ENSURE_SUCCESS(rv, rv);

    rv = mBuilder->AddCriterion(notEmptyString);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = mBuilder->AddGroupBy(MEDIAITEMS_ALIAS, columnName);
    NS_ENSURE_SUCCESS(rv, rv);

  }
  else {
    if (mSorts->ElementAt(0).property.Equals(ORDINAL_PROPERTY)) {

      if (mBaseTable.Equals(SIMPLEMEDIALISTS_TABLE)) {
        rv = mBuilder->AddGroupBy(CONSTRAINT_ALIAS,
                                  ORDINAL_COLUMN);
        NS_ENSURE_SUCCESS(rv, rv);
      }
    }
    else {
      nsCOMPtr<sbISQLBuilderCriterion> notEmptyString;
      rv = mBuilder->CreateMatchCriterionString(SORT_ALIAS,
                                                OBJSORTABLE_COLUMN,
                                                sbISQLSelectBuilder::MATCH_NOTEQUALS,
                                                EmptyString(),
                                                getter_AddRefs(notEmptyString));
      NS_ENSURE_SUCCESS(rv, rv);

      rv = mBuilder->AddCriterion(notEmptyString);
      NS_ENSURE_SUCCESS(rv, rv);

      rv = mBuilder->AddGroupBy(SORT_ALIAS, OBJSORTABLE_COLUMN);
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }

  return NS_OK;
}

nsresult
sbLocalDatabaseQuery::AddJoinToGetNulls()
{
  nsresult rv;

  if (SB_IsTopLevelProperty(mSorts->ElementAt(0).property)) {
    nsAutoString columnName;
    rv = SB_GetTopLevelPropertyColumn(mSorts->ElementAt(0).property, columnName);
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<sbISQLBuilderCriterion> criterion;
    rv = mBuilder->CreateMatchCriterionNull(MEDIAITEMS_ALIAS,
                                            columnName,
                                            sbISQLSelectBuilder::MATCH_EQUALS,
                                            getter_AddRefs(criterion));
    NS_ENSURE_SUCCESS(rv, rv);

    rv = mBuilder->AddCriterion(criterion);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  else {
    nsCOMPtr<sbISQLBuilderCriterion> criterionGuid;
    rv = mBuilder->CreateMatchCriterionTable(GETNULL_ALIAS,
                                             MEDIAITEMID_COLUMN,
                                             sbISQLSelectBuilder::MATCH_EQUALS,
                                             MEDIAITEMS_ALIAS,
                                             MEDIAITEMID_COLUMN,
                                             getter_AddRefs(criterionGuid));
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<sbISQLBuilderCriterion> criterionProperty;
    rv = mBuilder->CreateMatchCriterionLong(GETNULL_ALIAS,
                                            PROPERTYID_COLUMN,
                                            sbISQLSelectBuilder::MATCH_EQUALS,
                                            GetPropertyId(mSorts->ElementAt(0).property),
                                            getter_AddRefs(criterionProperty));
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<sbISQLBuilderCriterion> criterion;
    rv = mBuilder->CreateAndCriterion(criterionGuid,
                                      criterionProperty,
                                      getter_AddRefs(criterion));
    NS_ENSURE_SUCCESS(rv, rv);

    rv = mBuilder->AddJoinWithCriterion(sbISQLSelectBuilder::JOIN_LEFT,
                                        PROPERTIES_TABLE,
                                        GETNULL_ALIAS,
                                        criterion);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = mBuilder->CreateMatchCriterionNull(GETNULL_ALIAS,
                                            OBJSORTABLE_COLUMN,
                                            sbISQLSelectBuilder::MATCH_EQUALS,
                                            getter_AddRefs(criterion));
    NS_ENSURE_SUCCESS(rv, rv);

    rv = mBuilder->AddCriterion(criterion);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

nsresult
sbLocalDatabaseQuery::AddResortColumns()
{
  nsresult rv;

  rv = mBuilder->SetDistinct(PR_TRUE);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mBuilder->AddColumn(MEDIAITEMS_ALIAS, MEDIAITEMID_COLUMN);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mBuilder->AddColumn(MEDIAITEMS_ALIAS, GUID_COLUMN);
  NS_ENSURE_SUCCESS(rv, rv);

  if (mIsFullLibrary) {
    rv = mBuilder->AddColumn(EmptyString(), NS_LITERAL_STRING("''"));
    NS_ENSURE_SUCCESS(rv, rv);

    rv = mBuilder->AddColumn(MEDIAITEMS_ALIAS, ROWID_COLUMN);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  else {
    rv = mBuilder->AddColumn(CONSTRAINT_ALIAS, ORDINAL_COLUMN);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = mBuilder->AddColumn(CONSTRAINT_ALIAS, ROWID_COLUMN);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

nsresult
sbLocalDatabaseQuery::AddMultiSorts()
{
  nsresult rv;
  PRUint32 numSorts = mSorts->Length();
  for (PRUint32 i = 1; i < numSorts; i++) {
    const sbLocalDatabaseGUIDArray::SortSpec& sort = mSorts->ElementAt(i);

    nsString joinedAlias(SORT_ALIAS);
    joinedAlias.AppendInt(i);

    nsCOMPtr<sbISQLBuilderCriterion> criterionGuid;
    rv = mBuilder->CreateMatchCriterionTable(joinedAlias,
                                             MEDIAITEMID_COLUMN,
                                             sbISQLSelectBuilder::MATCH_EQUALS,
                                             MEDIAITEMS_ALIAS,
                                             MEDIAITEMID_COLUMN,
                                             getter_AddRefs(criterionGuid));
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<sbISQLBuilderCriterion> criterion;
    if (SB_IsTopLevelProperty(sort.property)) {
      rv = mBuilder->AddJoinWithCriterion(sbISQLSelectBuilder::JOIN_INNER,
                                          MEDIAITEMS_TABLE,
                                          joinedAlias,
                                          criterion);
      NS_ENSURE_SUCCESS(rv, rv);

      nsString columnName;
      rv = SB_GetTopLevelPropertyColumn(sort.property, columnName);
      NS_ENSURE_SUCCESS(rv, rv);

      mBuilder->AddOrder(joinedAlias, columnName, sort.ascending);
      NS_ENSURE_SUCCESS(rv, rv);
    }
    else {
      nsCOMPtr<sbISQLBuilderCriterion> criterionProperty;
      rv = mBuilder->CreateMatchCriterionLong(joinedAlias,
                                              NS_LITERAL_STRING("property_id"),
                                              sbISQLSelectBuilder::MATCH_EQUALS,
                                              GetPropertyId(sort.property),
                                              getter_AddRefs(criterionProperty));
      NS_ENSURE_SUCCESS(rv, rv);

      rv = mBuilder->CreateAndCriterion(criterionGuid,
                                        criterionProperty,
                                        getter_AddRefs(criterion));
      NS_ENSURE_SUCCESS(rv, rv);

      rv = mBuilder->AddJoinWithCriterion(sbISQLSelectBuilder::JOIN_LEFT,
                                          PROPERTIES_TABLE,
                                          joinedAlias,
                                          criterion);
      NS_ENSURE_SUCCESS(rv, rv);

      rv = mBuilder->AddOrder(joinedAlias, OBJSORTABLE_COLUMN, sort.ascending);
      NS_ENSURE_SUCCESS(rv, rv);
    }

  }

  // Sort on the base table's media_item_id column so make sure things are
  // sorted the same on all platforms
  rv = mBuilder->AddOrder(MEDIAITEMS_ALIAS, MEDIAITEMID_COLUMN, PR_TRUE);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

PRInt32
sbLocalDatabaseQuery::GetPropertyId(const nsAString& aProperty)
{
  return SB_GetPropertyId(aProperty, mPropertyCache);
}

void
sbLocalDatabaseQuery::MaxExpr(const nsAString& aAlias,
                              const nsAString& aColumn,
                              nsAString& aExpr)
{
  nsAutoString buff;
  buff.AssignLiteral("max(");
  buff.Append(aAlias);
  buff.AppendLiteral(".");
  buff.Append(aColumn);
  buff.AppendLiteral(")");

  aExpr = buff;
}

