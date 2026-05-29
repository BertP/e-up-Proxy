import requests

ha_url = "http://192.168.1.251:8123/api/error_log"
token = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJiMTI0MGU4OTRlNTM0M2M1YWQ0NDkzZWY0Y2VkMjI5NyIsImlhdCI6MTc3OTg2NDQ5MiwiZXhwIjoyMDk1MjI0NDkyfQ.IZaiKWpgY5E_xZLZYhCoG0uWzfAIqbsIru2EXmTBmX8"

headers = {
    "Authorization": f"Bearer {token}",
}

try:
    response = requests.get(ha_url, headers=headers, timeout=5)
    if response.status_code == 200:
        log_content = response.text
        lines = log_content.split("\n")
        eup_lines = [l for l in lines if "eup" in l.lower() or "mqtt" in l.lower() or "discovery" in l.lower()]
        print(f"=== Found {len(eup_lines)} related lines in HA Error Log ===")
        for l in eup_lines[-20:]:  # Print last 20 lines
            print(l)
    else:
        print(f"Failed to query HA logs. Status code: {response.status_code}")
except Exception as e:
    print(f"Error querying HA logs: {e}")
