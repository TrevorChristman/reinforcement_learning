import rl_client

class my_error_callback(rl_client.error_callback):
    def on_error(self, error_code, error_message):
        print("Background error:")
        print(error_message)

def load_config_from_json(file_name):
    with open(file_name, 'r') as config_file:
        return rl_client.create_config_from_json(config_file.read())

def main():
    config = load_config_from_json("client.json")

    test_cb = my_error_callback()
    model = rl_client.live_model(config, test_cb)
    model.init()

    uuid = "uuid"
    context = '{"User":{"id":"a","major":"eng","hobby":"hiking"},"_multi":[{"a1":"f1"},{"a2":"f2"}]}'

    model_id, chosen_action_id, action_probabilities = model.choose_rank(uuid, context)

    print("uuid: " + uuid)
    print("model_id: " + model_id)
    print("chosen action id: " + str(chosen_action_id))
    print("all action probabilities " + str(action_probabilities))

    model_id, chosen_action_id, action_probabilities, uuid = model.choose_rank(context)

    print("uuid: " + uuid)
    print("model_id: " + model_id)
    print("chosen action id: " + str(chosen_action_id))
    print("all action probabilities " + str(action_probabilities))

    reward = 1.0
    model.report_outcome(uuid, reward)

if __name__ == "__main__":
   main()
