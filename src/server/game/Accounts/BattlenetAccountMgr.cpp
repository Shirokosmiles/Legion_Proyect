/*
 * Copyright (C) 2008-2014 TrinityCore <http://www.trinitycore.org/>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "AccountMgr.h"
#include "BattlenetAccountMgr.h"
#include "DatabaseEnv.h"
#include "Util.h"
#include "SHA256.h"

AccountOpResult Battlenet::AccountMgrNet::CreateBattlenetAccount(std::string email, std::string password, bool withGameAccount, std::string* gameAccountName)
{
    if (utf8length(email) > MAX_BNET_EMAIL_STR)
        return AccountOpResult::AOR_NAME_TOO_LONG;

    if (utf8length(password) > MAX_PASS_STR)
        return AccountOpResult::AOR_PASS_TOO_LONG;

    Utf8ToUpperOnlyLatin(email);
    Utf8ToUpperOnlyLatin(password);

    if (GetId(email))
         return AccountOpResult::AOR_NAME_ALREADY_EXIST;

    PreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_INS_BNET_ACCOUNT);
    stmt->setString(0, email);
    stmt->setString(1, CalculateShaPassHash(email, password));
    LoginDatabase.DirectExecute(stmt);

	uint32 newAccountId = GetId(email);
	ASSERT(newAccountId);


	if (withGameAccount)
	{
		*gameAccountName = std::to_string(newAccountId) + "#1";
		AccountMgr::CreateAccount(*gameAccountName, password, newAccountId, 1);
	}

    return AccountOpResult::AOR_OK;
}

AccountOpResult Battlenet::AccountMgrNet::ChangePassword(uint32 accountId, std::string newPassword)
{
    std::string username;
    if (!GetName(accountId, username))
        return AccountOpResult::AOR_NAME_NOT_EXIST;                          // account doesn't exist

    Utf8ToUpperOnlyLatin(username);
    Utf8ToUpperOnlyLatin(newPassword);
    if (utf8length(newPassword) > MAX_PASS_STR)
        return AccountOpResult::AOR_PASS_TOO_LONG;

    PreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_UPD_BNET_PASSWORD);
    stmt->setString(0, CalculateShaPassHash(username, newPassword));
    stmt->setUInt32(1, accountId);
    LoginDatabase.Execute(stmt);

    return AccountOpResult::AOR_OK;
}

bool Battlenet::AccountMgrNet::CheckPassword(uint32 accountId, std::string password)
{
    std::string username;

    if (!GetName(accountId, username))
        return false;

    Utf8ToUpperOnlyLatin(username);
    Utf8ToUpperOnlyLatin(password);

    PreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_BNET_CHECK_PASSWORD);
    stmt->setUInt32(0, accountId);
    stmt->setString(1, CalculateShaPassHash(username, password));

    return LoginDatabase.Query(stmt) != nullptr;
}

AccountOpResult Battlenet::AccountMgrNet::LinkWithGameAccount(std::string const& email, std::string const& gameAccountName)
{
    uint32 bnetAccountId = GetId(email);
    if (!bnetAccountId)
        return AccountOpResult::AOR_NAME_NOT_EXIST;

    uint32 gameAccountId = AccountMgrNet::GetId(gameAccountName);
    if (!gameAccountId)
        return AccountOpResult::AOR_NAME_NOT_EXIST;

    if (GetIdByGameAccount(gameAccountId))
        return AccountOpResult::AOR_ACCOUNT_BAD_LINK;

    PreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_UPD_BNET_GAME_ACCOUNT_LINK);
    stmt->setUInt32(0, bnetAccountId);
    stmt->setUInt8(1, GetMaxIndex(bnetAccountId) + 1);
    stmt->setUInt32(2, gameAccountId);
    LoginDatabase.Execute(stmt);
    return AccountOpResult::AOR_OK;
}

AccountOpResult Battlenet::AccountMgrNet::UnlinkGameAccount(std::string const& gameAccountName)
{
    uint32 gameAccountId = AccountMgrNet::GetId(gameAccountName);
    if (!gameAccountId)
        return AccountOpResult::AOR_NAME_NOT_EXIST;

    if (!GetIdByGameAccount(gameAccountId))
        return AccountOpResult::AOR_ACCOUNT_BAD_LINK;

    PreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_UPD_BNET_GAME_ACCOUNT_LINK);
    stmt->setNull(0);
    stmt->setNull(1);
    stmt->setUInt32(2, gameAccountId);
    LoginDatabase.Execute(stmt);
    return AccountOpResult::AOR_OK;
}

uint32 Battlenet::AccountMgrNet::GetId(std::string const& username)
{
    PreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_BNET_ACCOUNT_ID_BY_EMAIL);
    stmt->setString(0, username);
    if (PreparedQueryResult result = LoginDatabase.Query(stmt))
        return (*result)[0].GetUInt32();

    return 0;
}

uint32 Battlenet::AccountMgrNet::GetIdByGameAccount(uint32 gameAccountId)
{
    PreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_BNET_ACCOUNT_ID_BY_GAME_ACCOUNT);
    stmt->setUInt32(0, gameAccountId);
    if (PreparedQueryResult result = LoginDatabase.Query(stmt))
        return (*result)[0].GetUInt32();

    return 0;
}

bool Battlenet::AccountMgrNet::GetName(uint32 accountId, std::string& name)
{
    PreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_BNET_ACCOUNT_EMAIL_BY_ID);
    stmt->setUInt32(0, accountId);
    if (PreparedQueryResult result = LoginDatabase.Query(stmt))
    {
        name = (*result)[0].GetString();
        return true;
    }

    return false;
}

uint8 Battlenet::AccountMgrNet::GetMaxIndex(uint32 accountId)
{
	PreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_BNET_MAX_ACCOUNT_INDEX);
	stmt->setUInt32(0, accountId);
	PreparedQueryResult result = LoginDatabase.Query(stmt);
	if (result)
		return (*result)[0].GetUInt8();

	return 0;
}

std::string Battlenet::AccountMgrNet::CalculateShaPassHash(std::string const& name, std::string const& password)
{
    SHA256Hash email;
    email.UpdateData(name);
    email.Finalize();

    SHA256Hash sha;
    sha.UpdateData(ByteArrayToHexStr(email.GetDigest(), email.GetLength()));
    sha.UpdateData(":");
    sha.UpdateData(password);
    sha.Finalize();

    return ByteArrayToHexStr(sha.GetDigest(), sha.GetLength(), true);
}

void Battlenet::AccountMgrNet::GetVoid()
{
}
