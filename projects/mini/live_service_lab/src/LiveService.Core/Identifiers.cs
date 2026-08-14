namespace LiveService.Core;

public static class Identifiers
{
    public static bool IsValid(string value, int maximumLength) =>
        !string.IsNullOrWhiteSpace(value) && value.Length <= maximumLength &&
        value.All(character =>
            character is >= 'a' and <= 'z' or >= 'A' and <= 'Z' or >= '0' and <= '9' or
            '-' or '_' or '.');
}
