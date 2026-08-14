using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace LiveService.Core;

public sealed class TokenService
{
    private readonly byte[] secret;

    public TokenService(ReadOnlySpan<byte> secret)
    {
        if (secret.Length < 32)
        {
            throw new ArgumentException("Token secret must be at least 32 bytes.", nameof(secret));
        }
        this.secret = secret.ToArray();
    }

    public string IssueAccessToken(string playerId, DateTimeOffset now, TimeSpan lifetime)
    {
        var payload = new AccessPayload("access", playerId, now.Add(lifetime).ToUnixTimeSeconds());
        return Sign(payload);
    }

    public bool TryValidateAccessToken(
        string token,
        DateTimeOffset now,
        out string playerId)
    {
        playerId = string.Empty;
        if (!TryVerify(token, out AccessPayload? payload) || payload is null ||
            payload.Kind != "access" || payload.ExpiresAt <= now.ToUnixTimeSeconds())
        {
            return false;
        }
        playerId = payload.PlayerId;
        return !string.IsNullOrWhiteSpace(playerId);
    }

    public string IssueGameToken(GameAdmission admission)
    {
        var payload = new GamePayload(
            "game",
            admission.PlayerId,
            admission.MatchId,
            admission.ServerId,
            admission.TokenId,
            admission.ExpiresAt.ToUnixTimeSeconds());
        return Sign(payload);
    }

    public bool TryValidateGameToken(
        string token,
        string expectedServerId,
        DateTimeOffset now,
        out GameAdmission? admission)
    {
        admission = null;
        if (!TryVerify(token, out GamePayload? payload) || payload is null ||
            payload.Kind != "game" || payload.ServerId != expectedServerId ||
            payload.ExpiresAt <= now.ToUnixTimeSeconds() ||
            string.IsNullOrWhiteSpace(payload.PlayerId) ||
            string.IsNullOrWhiteSpace(payload.MatchId) ||
            string.IsNullOrWhiteSpace(payload.TokenId))
        {
            return false;
        }
        admission = new GameAdmission(
            payload.PlayerId,
            payload.MatchId,
            payload.ServerId,
            payload.TokenId,
            DateTimeOffset.FromUnixTimeSeconds(payload.ExpiresAt));
        return true;
    }

    private string Sign<T>(T payload)
    {
        byte[] json = JsonSerializer.SerializeToUtf8Bytes(payload);
        string body = Base64UrlEncode(json);
        byte[] signature = HMACSHA256.HashData(secret, Encoding.ASCII.GetBytes(body));
        return body + "." + Base64UrlEncode(signature);
    }

    private bool TryVerify<T>(string token, out T? payload)
    {
        payload = default;
        int separator = token.IndexOf('.');
        if (separator <= 0 || separator != token.LastIndexOf('.')) return false;
        string body = token[..separator];
        string signatureText = token[(separator + 1)..];
        try
        {
            byte[] supplied = Base64UrlDecode(signatureText);
            byte[] expected = HMACSHA256.HashData(secret, Encoding.ASCII.GetBytes(body));
            if (supplied.Length != expected.Length ||
                !CryptographicOperations.FixedTimeEquals(supplied, expected)) return false;
            payload = JsonSerializer.Deserialize<T>(Base64UrlDecode(body));
            return payload is not null;
        }
        catch (FormatException)
        {
            return false;
        }
        catch (JsonException)
        {
            return false;
        }
    }

    private static string Base64UrlEncode(ReadOnlySpan<byte> bytes) =>
        Convert.ToBase64String(bytes).TrimEnd('=').Replace('+', '-').Replace('/', '_');

    private static byte[] Base64UrlDecode(string text)
    {
        string padded = text.Replace('-', '+').Replace('_', '/');
        padded += (padded.Length % 4) switch
        {
            0 => string.Empty,
            2 => "==",
            3 => "=",
            _ => throw new FormatException("Invalid base64url length."),
        };
        return Convert.FromBase64String(padded);
    }

    private sealed record AccessPayload(string Kind, string PlayerId, long ExpiresAt);
    private sealed record GamePayload(
        string Kind,
        string PlayerId,
        string MatchId,
        string ServerId,
        string TokenId,
        long ExpiresAt);
}
