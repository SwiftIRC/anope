/* NickServ core functions
 *
 * (C) 2003-2010 Anope Team
 * Contact us at team@anope.org
 *
 * Please read COPYING and README for further details.
 *
 * Based on the original code of Epona by Lara.
 * Based on the original code of Services by Andy Church.
 */

/*************************************************************************/

#include "module.h"

static bool SendResetEmail(User *u, NickAlias *na);

class CommandNSResetPass : public Command
{
 public:
	CommandNSResetPass() : Command("RESETPASS", 1, 1)
	{
		this->SetFlag(CFLAG_ALLOW_UNREGISTERED);
	}

	CommandReturn Execute(CommandSource &source, const std::vector<Anope::string> &params)
	{
		User *u = source.u;
		NickAlias *na;

		if (Config->RestrictMail && (!u->Account() || !u->Account()->HasCommand("nickserv/resetpass")))
			source.Reply(ACCESS_DENIED);
		if (!(na = findnick(params[0])))
			source.Reply(NICK_X_NOT_REGISTERED, params[0].c_str());
		else if (na->HasFlag(NS_FORBIDDEN))
			source.Reply(NICK_X_FORBIDDEN, na->nick.c_str());
		else
		{
			if (SendResetEmail(u, na))
			{
				Log(LOG_COMMAND, u, this) << "for " << na->nick << " (group: " << na->nc->display << ")";
				source.Reply(NICK_RESETPASS_COMPLETE, na->nick.c_str());
			}
		}

		return MOD_CONT;
	}

	bool OnHelp(CommandSource &source, const Anope::string &subcommand)
	{
		source.Reply(NICK_HELP_RESETPASS);
		return true;
	}

	void OnSyntaxError(CommandSource &source, const Anope::string &subcommand)
	{
		SyntaxError(source, "RESETPASS", NICK_RESETPASS_SYNTAX);
	}

	void OnServHelp(CommandSource &source)
	{
		source.Reply(NICK_HELP_CMD_RESETPASS);
	}
};

class NSResetPass : public Module
{
	CommandNSResetPass commandnsresetpass;

 public:
	NSResetPass(const Anope::string &modname, const Anope::string &creator) : Module(modname, creator)
	{
		if (!Config->UseMail)
			throw ModuleException("Not using mail.");

		this->SetAuthor("Anope");
		this->SetType(CORE);

		this->AddCommand(NickServ, &commandnsresetpass);

		ModuleManager::Attach(I_OnPreCommand, this);
	}

	EventReturn OnPreCommand(CommandSource &source, Command *command, const std::vector<Anope::string> &params)
	{
		User *u = source.u;
		BotInfo *service = source.owner;
		if (service == NickServ && command->name.equals_ci("CONFIRM") && !params.empty())
		{
			NickAlias *na = findnick(source.u->nick);

			time_t t;
			Anope::string c;
			if (na && na->nc->GetExtRegular("ns_resetpass_code", c) && na->nc->GetExtRegular("ns_resetpass_time", t))
			{
				if (t < Anope::CurTime - 3600)
				{
					na->nc->Shrink("ns_resetpass_code");
					na->nc->Shrink("ns_resetpass_time");
					source.Reply(NICK_CONFIRM_EXPIRED);
					return EVENT_STOP;
				}

				Anope::string passcode = params[0];
				if (passcode.equals_cs(c))
				{
					na->nc->Shrink("ns_resetpass_code");
					na->nc->Shrink("ns_resetpass_time");

					u->UpdateHost();
					na->last_realname = u->realname;
					na->last_seen = Anope::CurTime;
					u->Login(na->nc);
					ircdproto->SendAccountLogin(u, u->Account());
					ircdproto->SetAutoIdentificationToken(u);
					u->SetMode(NickServ, UMODE_REGISTERED);
					FOREACH_MOD(I_OnNickIdentify, OnNickIdentify(u));

					Log(LOG_COMMAND, u, &commandnsresetpass) << "confirmed RESETPASS to forcefully identify to " << na->nick;
					source.Reply(NICK_CONFIRM_SUCCESS, Config->s_NickServ.c_str());

					if (ircd->vhost)
						do_on_id(u);
					if (Config->NSModeOnID)
						do_setmodes(u);
					check_memos(u);
				}
				else
				{
					Log(LOG_COMMAND, u, &commandnsresetpass) << "invalid confirm passcode for " << na->nick;
					source.Reply(NICK_CONFIRM_INVALID);
					bad_password(u);
				}

				return EVENT_STOP;
			}
		}

		return EVENT_CONTINUE;
	}
};

static bool SendResetEmail(User *u, NickAlias *na)
{
	char subject[BUFSIZE], message[BUFSIZE];

	snprintf(subject, sizeof(subject), GetString(na->nc, NICK_RESETPASS_SUBJECT).c_str(), na->nick.c_str());

	int min = 1, max = 62;
	int chars[] = {
		' ', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l',
		'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x',
		'z', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L',
		'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y',
		'Z', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9'
	};

	Anope::string passcode;
	int idx;
	for (idx = 0; idx < 20; ++idx)
		passcode += chars[1 + static_cast<int>((static_cast<float>(max - min)) * getrandom16() / 65536.0) + min];

	snprintf(message, sizeof(message), GetString(na->nc, NICK_RESETPASS_MESSAGE).c_str(), na->nick.c_str(), Config->s_NickServ.c_str(), passcode.c_str(), Config->NetworkName.c_str());

	na->nc->Shrink("ns_resetpass_code");
	na->nc->Shrink("ns_resetpass_time");

	na->nc->Extend("ns_resetpass_code", new ExtensibleItemRegular<Anope::string>(passcode));
	na->nc->Extend("ns_resetpass_time", new ExtensibleItemRegular<time_t>(Anope::CurTime));

	return Mail(u, na->nc, NickServ, subject, message);
}

MODULE_INIT(NSResetPass)