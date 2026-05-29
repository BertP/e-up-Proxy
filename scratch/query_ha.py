import requests
import json

ha_url = "http://192.168.1.251:8123/api/states"
token = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJiMTI0MGU4OTRlNTM0M2M1YWQ0NDkzZWY0Y2VkMjI5NyIsImlhdCI6MTc3OTg2NDQ5MiwiZXhwIjoyMDk1MjI0NDkyfQ.IZaiKWpgY5E_xZLZYhCoG0uWzfAIqbsIru2EXmTBmX8"

headers = {
    "Authorization": f"Bearer {token}",
    "content-type": "application/json",
}

try:
    response = requests.get(ha_url, headers=headers, timeout=5)
    if response.status_code == 200:
        states = response.json()
        eup_entities = [s for s in states if "e_up_proxy" in s["entity_id"]]
        print(f"=== Found {len(eup_entities)} e-up! proxy entities in HA ===")
        for ent in eup_entities:
            print(f"Entity: {ent['entity_id']}")
            print(f"  State: {ent['state']}")
            print(f"  Friendly Name: {ent['attributes'].get('friendly_name', 'N/A')}")
            print(f"  Last Updated: {ent.get('last_updated', 'N/A')}\n")
    else:
        print(f"Failed to query HA API. Status code: {response.status_code}")
except Exception as e:
    print(f"Error querying HA API: {e}")
